#include "Dwarf.h"

#include <map>

const DwarfSpelling kElfDwarf = {
    "  .section .debug_abbrev,\"\",@progbits\n",
    "  .section .debug_info,\"\",@progbits\n",
    "  .section .debug_line,\"\",@progbits\n",
    true,
    6,
    "",
};

const DwarfSpelling kMachODwarf = {
    "  .section __DWARF,__debug_abbrev,regular,debug\n",
    "  .section __DWARF,__debug_info,regular,debug\n",
    "  .section __DWARF,__debug_line,regular,debug\n",
    false,
    29,
    "_",
};

namespace {

const int kTagArray = 0x01, kTagStructure = 0x13, kTagUnion = 0x17;
const int kTagMember = 0x0d, kTagPointer = 0x0f, kTagCompileUnit = 0x11;
const int kTagSubroutine = 0x15, kTagSubrange = 0x21, kTagBase = 0x24;
const int kTagFormalParameter = 0x05, kTagSubprogram = 0x2e, kTagVariable = 0x34;
const int kTagLexicalBlock = 0x0b;
const int kTagUnspecifiedParameters = 0x18;

const int kAtLocation = 0x02, kAtName = 0x03, kAtByteSize = 0x0b;
const int kAtBitSize = 0x0d, kAtStmtList = 0x10, kAtLowPc = 0x11;
const int kAtHighPc = 0x12, kAtLanguage = 0x13, kAtCompDir = 0x1b;
const int kAtProducer = 0x25, kAtUpperBound = 0x2f, kAtDataMemberLocation = 0x38;
const int kAtDeclFile = 0x3a, kAtDeclLine = 0x3b, kAtEncoding = 0x3e;
const int kAtExternal = 0x3f, kAtFrameBase = 0x40, kAtType = 0x49;
const int kAtDataBitOffset = 0x6b;

const int kFormAddr = 0x01, kFormData2 = 0x05, kFormData4 = 0x06;
const int kFormString = 0x08, kFormData1 = 0x0b, kFormFlag = 0x0c;
const int kFormRef4 = 0x13, kFormSecOffset = 0x17, kFormExprLoc = 0x18;

// DW_LANG_C_plus_plus. It was DW_LANG_C89 - 0x01, inherited from the compiler
// this was forked from - which is a debugger being told the wrong language by a
// C++ compiler, and it decides how a name is printed and demangled.
const int kLangCpp = 0x04;
const int kAteFloat = 0x04, kAteSigned = 0x05, kAteSignedChar = 0x06;
const int kAteUnsigned = 0x07, kAteUnsignedChar = 0x08;

const int kOpAddr = 0x03, kOpFbreg = 0x91;

enum Abbrev {
    kAbCompileUnit = 1,
    kAbSubprogram, kAbSubprogramVoid,
    kAbFormalParameter, kAbVariable, kAbStaticVariable,
    kAbBase, kAbPointer, kAbPointerVoid,
    kAbArray, kAbSubrange, kAbSubrangeOpen,
    kAbStructure, kAbUnion, kAbMember, kAbBitField,
    kAbSubroutine, kAbSubroutineVoid, kAbParamType, kAbUnspecified,
    kAbLexicalBlock
};

void line(std::string &o, const std::string &text) { o += text; o += '\n'; }

void num(std::string &o, const char *dir, long long v) {
    o += dir;
    o += ' ';
    o += std::to_string(v);
    o += '\n';
}

// Escaped as CodeView's are: a literal operator's name, `operator"" _km`, holds two quotes.
void str(std::string &o, const std::string &v) {
    o += "  .asciz \"";
    for (char c : v) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    o += "\"\n";
}

void bytes(std::string &o, const std::vector<unsigned char> &b) {
    for (std::size_t i = 0; i < b.size(); i++)
        num(o, "  .byte", b[i]);
}

void sleb(std::vector<unsigned char> &out, long long v) {
    bool more = true;
    while (more) {
        unsigned char byte = static_cast<unsigned char>(v & 0x7f);
        v >>= 7;
        bool signBit = (byte & 0x40) != 0;
        if ((v == 0 && !signBit) || (v == -1 && signBit)) more = false;
        else byte |= 0x80;
        out.push_back(byte);
    }
}

void attr(std::string &o, int at, int form) {
    o += "  .byte ";
    o += std::to_string(at);
    o += ", ";
    o += std::to_string(form);
    o += '\n';
}

void abbrev(std::string &o, int code, int tag, bool children) {
    num(o, "  .byte", code);
    num(o, "  .byte", tag);
    num(o, "  .byte", children ? 1 : 0);
}

void endAbbrev(std::string &o) { o += "  .byte 0, 0\n"; }

class Types {
public:

    int id(const Type *t) {
        if (t == nullptr || t->isVoid()) return 0;
        std::map<const Type *, int>::iterator it = ids_.find(t);
        if (it != ids_.end()) return it->second;

        int n = static_cast<int>(order_.size()) + 1;
        ids_[t] = n;
        order_.push_back(t);

        if (t->isPointer() || t->isArray()) id(t->pointee());
        if (t->isFunction()) {
            id(t->returns());
            for (std::size_t i = 0; i < t->params().size(); i++)
                id(t->params()[i]);
        }
        if (t->isStructOrUnion())
            for (std::size_t i = 0; i < t->members().size(); i++)
                id(t->members()[i].type);
        return n;
    }

    std::size_t size() const { return order_.size(); }
    const Type *at(std::size_t i) const { return order_[i]; }

private:
    std::map<const Type *, int> ids_;
    std::vector<const Type *> order_;
};

std::string label(int id) { return "Ldwarf.t" + std::to_string(id); }

void typeRef(std::string &o, int id) {
    line(o, "  .long " + label(id) + " - Ldebug.cu.begin");
}

int encodingFor(const Type *t, const Target &target) {
    if (t->isFloating()) return kAteFloat;
    if (t->kind() == Kind::Char)
        return target.plainCharIsSigned() ? kAteSignedChar : kAteUnsignedChar;
    if (t->kind() == Kind::SChar) return kAteSignedChar;
    if (t->kind() == Kind::UChar) return kAteUnsignedChar;
    return t->isSigned(target) ? kAteSigned : kAteUnsigned;
}

std::vector<unsigned char> frameLocation(int offset, const DwarfFunction &f) {
    std::vector<unsigned char> e;
    e.push_back(kOpFbreg);
    sleb(e, (f.frameBaseReg >= 0 ? f.frameBias : 0) - static_cast<long long>(offset));
    return e;
}

void exprLoc(std::string &o, const std::vector<unsigned char> &e) {
    num(o, "  .byte", static_cast<long long>(e.size()));
    bytes(o, e);
}

void addressLoc(std::string &o, const std::string &symbol) {
    num(o, "  .byte", 9);
    num(o, "  .byte", kOpAddr);
    line(o, "  .quad " + symbol);
}

void writeAbbrevTable(std::string &o, const DwarfSpelling &sp) {
    o += sp.abbrev;
    if (sp.offsetsAreLabels) line(o, "Ldebug.abbrev.begin:");

    abbrev(o, kAbCompileUnit, kTagCompileUnit, true);
    attr(o, kAtProducer, kFormString);
    attr(o, kAtLanguage, kFormData1);
    attr(o, kAtName, kFormString);
    attr(o, kAtCompDir, kFormString);
    attr(o, kAtLowPc, kFormAddr);
    attr(o, kAtHighPc, kFormAddr);
    attr(o, kAtStmtList, kFormSecOffset);
    endAbbrev(o);

    for (int i = 0; i < 2; i++) {
        abbrev(o, i == 0 ? kAbSubprogram : kAbSubprogramVoid, kTagSubprogram, true);
        attr(o, kAtName, kFormString);
        attr(o, kAtDeclFile, kFormData1);
        attr(o, kAtDeclLine, kFormData2);
        attr(o, kAtLowPc, kFormAddr);
        attr(o, kAtHighPc, kFormAddr);
        attr(o, kAtFrameBase, kFormExprLoc);
        attr(o, kAtExternal, kFormFlag);
        if (i == 0) attr(o, kAtType, kFormRef4);
        endAbbrev(o);
    }

    abbrev(o, kAbLexicalBlock, kTagLexicalBlock, true);
    attr(o, kAtLowPc, kFormAddr);
    attr(o, kAtHighPc, kFormAddr);
    endAbbrev(o);

    abbrev(o, kAbFormalParameter, kTagFormalParameter, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtType, kFormRef4);
    attr(o, kAtLocation, kFormExprLoc);
    endAbbrev(o);

    abbrev(o, kAbVariable, kTagVariable, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtType, kFormRef4);
    attr(o, kAtLocation, kFormExprLoc);
    endAbbrev(o);

    abbrev(o, kAbStaticVariable, kTagVariable, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtType, kFormRef4);
    attr(o, kAtLocation, kFormExprLoc);
    attr(o, kAtExternal, kFormFlag);
    endAbbrev(o);

    abbrev(o, kAbBase, kTagBase, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtEncoding, kFormData1);
    attr(o, kAtByteSize, kFormData1);
    endAbbrev(o);

    abbrev(o, kAbPointer, kTagPointer, false);
    attr(o, kAtByteSize, kFormData1);
    attr(o, kAtType, kFormRef4);
    endAbbrev(o);

    abbrev(o, kAbPointerVoid, kTagPointer, false);
    attr(o, kAtByteSize, kFormData1);
    endAbbrev(o);

    abbrev(o, kAbArray, kTagArray, true);
    attr(o, kAtType, kFormRef4);
    endAbbrev(o);

    abbrev(o, kAbSubrange, kTagSubrange, false);
    attr(o, kAtUpperBound, kFormData4);
    endAbbrev(o);

    abbrev(o, kAbSubrangeOpen, kTagSubrange, false);
    endAbbrev(o);

    abbrev(o, kAbStructure, kTagStructure, true);
    attr(o, kAtName, kFormString);
    attr(o, kAtByteSize, kFormData4);
    endAbbrev(o);

    abbrev(o, kAbUnion, kTagUnion, true);
    attr(o, kAtName, kFormString);
    attr(o, kAtByteSize, kFormData4);
    endAbbrev(o);

    abbrev(o, kAbMember, kTagMember, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtType, kFormRef4);
    attr(o, kAtDataMemberLocation, kFormData4);
    endAbbrev(o);

    abbrev(o, kAbBitField, kTagMember, false);
    attr(o, kAtName, kFormString);
    attr(o, kAtType, kFormRef4);
    attr(o, kAtBitSize, kFormData1);
    attr(o, kAtDataBitOffset, kFormData4);
    endAbbrev(o);

    for (int i = 0; i < 2; i++) {
        abbrev(o, i == 0 ? kAbSubroutine : kAbSubroutineVoid, kTagSubroutine, true);
        if (i == 0) attr(o, kAtType, kFormRef4);
        endAbbrev(o);
    }

    abbrev(o, kAbParamType, kTagFormalParameter, false);
    attr(o, kAtType, kFormRef4);
    endAbbrev(o);

    abbrev(o, kAbUnspecified, kTagUnspecifiedParameters, false);
    endAbbrev(o);

    o += "  .byte 0\n";
}

void writeType(std::string &o, const Type *t, int id, Types &types,
               const Target &target) {
    line(o, label(id) + ":");

    if (t->isPointer()) {
        int to = types.id(t->pointee());
        num(o, "  .byte", to != 0 ? kAbPointer : kAbPointerVoid);
        num(o, "  .byte", t->size(target));
        if (to != 0) typeRef(o, to);
        return;
    }
    if (t->isArray()) {
        num(o, "  .byte", kAbArray);
        typeRef(o, types.id(t->pointee()));
        if (t->length() >= 1) {
            num(o, "  .byte", kAbSubrange);
            num(o, "  .long", t->length() - 1);
        } else {
            num(o, "  .byte", kAbSubrangeOpen);
        }
        o += "  .byte 0\n";
        return;
    }
    if (t->isFunction()) {
        int to = types.id(t->returns());
        num(o, "  .byte", to != 0 ? kAbSubroutine : kAbSubroutineVoid);
        if (to != 0) typeRef(o, to);
        const std::vector<const Type *> &ps = t->params();
        for (std::size_t i = 0; i < ps.size(); i++) {
            num(o, "  .byte", kAbParamType);
            typeRef(o, types.id(ps[i]));
        }
        if (t->isVariadicFn()) num(o, "  .byte", kAbUnspecified);
        o += "  .byte 0\n";
        return;
    }
    if (t->isStructOrUnion()) {
        num(o, "  .byte", t->kind() == Kind::Struct ? kAbStructure : kAbUnion);
        str(o, t->tag());
        num(o, "  .long", t->size(target));
        const std::vector<Member> &ms = t->members();
        for (std::size_t i = 0; i < ms.size(); i++) {
            if (ms[i].isBitField()) {
                num(o, "  .byte", kAbBitField);
                str(o, ms[i].name);
                typeRef(o, types.id(ms[i].type));
                num(o, "  .byte", ms[i].width);
                num(o, "  .long", ms[i].offset * 8 + ms[i].bitOffset);
            } else {
                num(o, "  .byte", kAbMember);
                str(o, ms[i].name);
                typeRef(o, types.id(ms[i].type));
                num(o, "  .long", ms[i].offset);
            }
        }
        o += "  .byte 0\n";
        return;
    }

    num(o, "  .byte", kAbBase);
    str(o, t->name());
    num(o, "  .byte", encodingFor(t, target));
    num(o, "  .byte", t->size(target));
}

void writeObject(std::string &o, const Local &l, Types &types,
                 const DwarfSpelling &sp, const DwarfFunction &f) {
    if (!l.staticName.empty()) {

        num(o, "  .byte", kAbStaticVariable);
        str(o, l.name);
        typeRef(o, types.id(l.type));
        addressLoc(o, sp.symbolPrefix + l.staticName);
        o += "  .byte 0\n";
        return;
    }
    num(o, "  .byte", l.isParam ? kAbFormalParameter : kAbVariable);
    str(o, l.name);
    typeRef(o, types.id(l.type));
    exprLoc(o, frameLocation(l.offset, f));
}

bool declaresAnything(const DwarfFunction &f, int scope) {
    if (f.locals != nullptr)
        for (std::size_t i = 0; i < f.locals->size(); i++)
            if ((*f.locals)[i].scope == scope) return true;
    for (std::size_t b = 1; b < f.blocks.size(); b++)
        if (f.blocks[b].parent == scope &&
            declaresAnything(f, static_cast<int>(b)))
            return true;
    return false;
}

void writeScope(std::string &o, const DwarfFunction &f, int scope,
                Types &types, const DwarfSpelling &sp) {
    if (f.locals != nullptr)
        for (std::size_t i = 0; i < f.locals->size(); i++)
            if ((*f.locals)[i].scope == scope)
                writeObject(o, (*f.locals)[i], types, sp, f);

    for (std::size_t b = 1; b < f.blocks.size(); b++) {
        if (f.blocks[b].parent != scope) continue;
        int id = static_cast<int>(b);
        if (!declaresAnything(f, id)) continue;

        if (f.blocks[b].begin.empty() || f.blocks[b].end.empty()) {
            writeScope(o, f, id, types, sp);
            continue;
        }
        num(o, "  .byte", kAbLexicalBlock);
        line(o, "  .quad " + f.blocks[b].begin);
        line(o, "  .quad " + f.blocks[b].end);
        writeScope(o, f, id, types, sp);
        o += "  .byte 0\n";
    }
}

}

void writeDwarf(std::string &out, const DwarfSpelling &sp, const Target &target,
                const std::string &file, const std::string &compDir,
                const std::vector<DwarfFunction> &fns,
                const std::vector<DwarfGlobal> &globals) {
    if (fns.empty()) return;

    writeAbbrevTable(out, sp);

    Types types;

    if (sp.offsetsAreLabels) {
        out += sp.line;
        line(out, "Ldebug.line.begin:");
    }

    out += sp.info;
    line(out, "Ldebug.cu.begin:");
    line(out, "  .long Ldebug.cu.end - Ldebug.cu.after.length");
    line(out, "Ldebug.cu.after.length:");
    out += "  .short 4\n";

    line(out, sp.offsetsAreLabels ? "  .long Ldebug.abbrev.begin" : "  .long 0");
    out += "  .byte 8\n";

    num(out, "  .byte", kAbCompileUnit);
    str(out, "cxx1");
    num(out, "  .byte", kLangCpp);
    str(out, file);
    str(out, compDir);
    line(out, "  .quad " + fns.front().begin);
    line(out, "  .quad " + fns.back().end);

    line(out, sp.offsetsAreLabels ? "  .long Ldebug.line.begin" : "  .long 0");

    std::vector<unsigned char> frameBase;
    frameBase.push_back(static_cast<unsigned char>(0x70 + sp.frameBaseReg));
    sleb(frameBase, 0);

    for (std::size_t i = 0; i < fns.size(); i++) {
        const DwarfFunction &f = fns[i];
        int returns = types.id(f.returns);
        num(out, "  .byte", returns != 0 ? kAbSubprogram : kAbSubprogramVoid);
        str(out, f.name);
        num(out, "  .byte", f.file);
        num(out, "  .short", f.line);
        line(out, "  .quad " + f.begin);
        line(out, "  .quad " + f.end);
        if (f.frameBaseReg < 0) {
            exprLoc(out, frameBase);
        } else {
            std::vector<unsigned char> own;
            own.push_back(static_cast<unsigned char>(0x70 + f.frameBaseReg));
            sleb(own, 0);
            exprLoc(out, own);
        }
        num(out, "  .byte", f.external ? 1 : 0);
        if (returns != 0) typeRef(out, returns);

        writeScope(out, f, 0, types, sp);
        out += "  .byte 0\n";
    }

    for (std::size_t i = 0; i < globals.size(); i++) {
        num(out, "  .byte", kAbStaticVariable);
        str(out, globals[i].name);
        typeRef(out, types.id(globals[i].type));
        addressLoc(out, sp.symbolPrefix + globals[i].symbol);
        num(out, "  .byte", globals[i].external ? 1 : 0);
    }

    for (std::size_t i = 0; i < types.size(); i++)
        writeType(out, types.at(i), static_cast<int>(i) + 1, types, target);

    out += "  .byte 0\n";
    line(out, "Ldebug.cu.end:");
}
