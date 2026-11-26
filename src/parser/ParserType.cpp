// The parser: types as they are written. Class, struct, union and enum definitions,
// the declaration specifiers in front of a declarator, and the declarator itself -
// the part of C++ grammar that reads inside out, and why this file is not smaller.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Operator.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// `T::type`, `Value<T>::type` - a member type reached through something that is
// already a type. **In a pattern the answer is a dependent member and not a
// lookup**; elsewhere it is looked up, and not finding it is what SFINAE is made of.
const Type *Parser::memberTypeWalk(const Type *t) {
    t = realizeMemberClass(t);
    while (peek().is("::") && (peekAt(1).kind == TokenKind::Ident ||
                               (peekAt(1).is("template") && peekAt(2).kind == TokenKind::Ident))) {
        // `Outer<T>::template Inner<int>` - [temp.names]/4's keyword before a member template's name.
        const std::size_t named = peekAt(1).is("template") ? 2 : 1;
        const std::string member = peekAt(named).text;
        if (patternOnly_ && dependentType(t)) {
            at_ += named + 1;
            if (named == 2 && peek().is("<")) skipTemplateArguments();
            t = types_.dependentMember(t, member);
            continue;
        }
        if (!t->isStructOrUnion()) break;
        t = realizeMemberClass(t, true);
        // [temp.mem]/1: `W<char>::N<double>`, a member class or alias template of the class reached.
        if (peekAt(named + 1).is("<")) {
            if (const TemplateDecl *mct = memberClassTemplateOf(t, member)) {
                const TemplateDecl decl = *mct;
                const std::size_t tpos = peekAt(named).pos;
                // [temp.names]/4, unless the qualifier is the class being read - the current instantiation.
                if (named == 1 && qualifierDependsOnParameter(at_) && !insideClass(t->unqualified()))
                    src_.fail(tpos, "'" + member + "' is a member template of a class that depends on a template "
                                    "parameter, and naming it with arguments there needs 'template' in front - "
                                    "[temp.names]/4");
                checkMemberTemplateAccess(decl, tpos);
                at_ += named + 1;
                t = instantiateClass(decl, tpos);
                continue;
            }
        }
        if (named == 2)
            src_.fail(peekAt(2).pos, "'" + member + "' is not a member class template of '" + t->tag() + "'");
        const Type *found = lookupInClass(t, member);
        // A member that exists and is not a type reads differently from a name the class lacks.
        // Inside sizeof or decltype this only tells parenHoldsTypeId the operand is an expression.
        if (found == nullptr && t->findMember(member) != nullptr)
            src_.fail(peekAt(1).pos, "'" + member + "' is a data member of '" + t->tag() +
                                     "', not a type");
        // [temp.res]/3: `typename` says the name it qualifies is a type, and a static member is not one.
        if (found == nullptr && typenameNamed_ && t->findStaticMember(member) != nullptr)
            src_.fail(peekAt(1).pos, "'" + t->tag() + "::" + member + "' is a static member, not a type - "
                                     "'typename' in front of it says it names one");
        // A static data member reached through a template's argument list,
        // `Box<int>::count`, is the refusal EXCLUSIONS names - not a missing
        // type (the cl review's A23); a typedef for the instantiation reaches it.
        if (found == nullptr && t->isSpecialization() && t->findStaticMember(member) != nullptr &&
            (peekAt(2).is("=") || peekAt(2).is(";")))
            src_.fail(peekAt(1).pos, "'" + t->tag() + "::" + member + "' names a static "
                                     "member through the template's argument list, which "
                                     "is not supported yet - a typedef for '" + t->tag() +
                                     "' reaches it: typedef " + t->tag() + " B; B::" + member);
        if (found == nullptr)
            src_.fail(peekAt(1).pos, "'" + t->tag() + "' has no member type "
                                     "called '" + member + "'");
        if (!currentFunction_.empty()) checkMemberTypeAccess(t, member, peekAt(1).pos);
        at_ += 2;
        t = realizeMemberClass(found);
    }
    return t;
}

Parser::Packs Parser::instantiatingPacks() const {
    Packs out;
    for (const TemplateParam &p : instantiatingParams_) {
        PackMembers members;
        const auto pb = packs_.find(p.name);
        if (p.isPack && pb != packs_.end()) {
            members = PackMembers(pb->second.types);
            members.values = pb->second.values;
        }
        out.push_back(members);
    }
    return out;
}

// The attributes of a class or an enumeration, after its key: `struct [[x]] S`. C++11's own two appertain to a
// function or a parameter and to nothing else ([dcl.attr.noreturn]/1, [dcl.attr.depend]/1), so here they are
// refused (1.80: `struct [[noreturn]] S;` was read and passed over); one this compiler does not know is ignored.
void Parser::skipTypeAttributes(const char *what) {
    const std::size_t from = at_;
    const std::size_t noreturnWas = noreturnAt_;
    if (!skipAttributes()) return;
    for (std::size_t i = from + 2; i < at_; i++)
        if ((tokens_[i].is("noreturn") || tokens_[i].is("carries_dependency")) &&
            (tokens_[i - 1].is("[") || tokens_[i - 1].is(",")) && !tokens_[i + 1].is("::"))
            src_.fail(tokens_[i].pos, "the attribute '" + tokens_[i].text + "' cannot be applied to " + what +
                                      " - [dcl.attr.noreturn]/1 and [dcl.attr.depend]/1 give it to a function "
                                      "or a parameter");
    noreturnAt_ = noreturnWas;
}

const Type *Parser::structOrUnionSpecifier(Kind kind, bool isClass) {
    const char *what = isClass ? "class" : (kind == Kind::Struct ? "struct" : "union");
    const std::size_t headAt = at_;
    skipTypeAttributes(isClass ? "a class" : (kind == Kind::Struct ? "a struct" : "a union"));
    std::size_t pos = peek().pos;
    // `#pragma pack(n)` in force here caps every alignment the layout reads.
    const int pack = src_.packAt(pos);
    if (pack != 0 && !target_.loadsUnaligned())
        src_.fail(pos, std::string("this ") + what + " is under '#pragma pack(" +
                       std::to_string(pack) + ")', and the C6000 backend loads words "
                       "aligned only - a packed member is not supported for this "
                       "target yet");
    struct Packed {
        static int to(int a, int cap) { return cap != 0 && a > cap ? cap : a; }
    };

    // `struct alignas(16) S` - the class's own alignment, folded into the
    // layout below as if a member had asked for it.
    int classAlign = 0;
    while (peek().is("alignas")) {
        int a = alignasSpecifier();
        if (a > classAlign) classAlign = a;
    }

    // **`struct lib::P *` and `struct lib::Box<char> *`**, [dcl.type.elab]: a qualified name, or a
    // template-id, names a class already declared - read as the type name it is, unless a body follows.
    if (classInstantiationTag_.empty() && peek().kind == TokenKind::Ident &&
        (peekAt(1).is("::") || (peekAt(1).is("<") && findTemplate(peek().text) != templates_.end()))) {
        std::size_t i = at_ + 1;
        int depth = 0;
        for (; i < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
            const Token &t = tokens_[i];
            if (depth > 0 && (t.is("(") || t.is("["))) { i = groupCloseAt(i); continue; }
            if (t.is("<")) depth++;
            else if (t.is(">")) depth--;
            else if (t.is(">>")) depth -= 2;
            else if (depth == 0 && !t.is("::") && t.kind != TokenKind::Ident) break;
            else if (depth == 0 && t.kind == TokenKind::Ident && !tokens_[i - 1].is("::")) break;
        }
        if (!tokens_[i].is("{") && !tokens_[i].is(":")) {
            StorageClass sc = StorageNone;
            Qualifiers q;
            const Type *named = unqualifiedSpecifiers(&sc, &q);
            if (!named->unqualified()->isStructOrUnion())
                src_.fail(pos, std::string("'") + what + "' names a class, and this is '" +
                               named->describe() + "'");
            return named;
        }
    }
    std::string tag;
    if (peek().kind == TokenKind::Ident) { tag = peek().text; at_++; }
    // **[class.union]/5: an anonymous union** - nameless, a body, a `;` straight after - its members the scope's own.
    const bool anonProper = kind == Kind::Union && tag.empty() && peek().is("{") && anonymousUnionAhead();

    // **`final` on a class head forbids deriving from it** - [class]/3 - recorded on the type
    // and checked by every later base clause. Contextual: only in front of the body is it the keyword.
    bool classFinal = false;
    if (peek().is("final") && (peekAt(1).is("{") || peekAt(1).is(":"))) { classFinal = true; at_++; }

    // **A class written inside another is named through it**: the tag becomes
    // "Outer::Inner", so it cannot collide with a global, with the single component
    // kept beside it. A *mention* names what is in scope; only a body qualifies.
    const Type *within = classStack_.empty() ? nullptr : classStack_.back();
    std::string local = tag;
    const bool defining = peek().is("{") || peek().is(":");

    // **A specialization is named by its whole argument list**: the tag becomes
    // "Box<int,3>", which nested classes had already made possible - the tag was an
    // arbitrary qualified string before templates ever needed one.
    std::string specializationOf;
    if (!classInstantiationTag_.empty() && defining) {
        // An explicit specialization has already had `Box<int>` read off it,
        // so there is no identifier here and the template's name comes along
        // beside the tag. An implicit one still has its `Box` in hand.
        specializationOf = classInstantiationOf_.empty() ? tag
                                                         : classInstantiationOf_;
        tag = classInstantiationTag_;
        local = specializationOf;
        classInstantiationTag_.clear();
        classInstantiationOf_.clear();
    }

    // [basic.lookup.elab]/2: `struct tm *` finds a visible `tm` first; only `struct X;` or a
    // name found nowhere declares one, and that one in the namespace, never the class.
    const bool forwardOnly = !defining && peek().is(";");
    bool introduced = false;
    if (!tag.empty() && !defining && !forwardOnly && specializationOf.empty()) {
        if (const Type *had = findTypedef(tag))
            if (had->isStructOrUnion() && had->localName() == local) return had;
        introduced = true;
    }
    if (within != nullptr && !tag.empty() && !introduced)
        tag = within->tag() + "::" + tag;

    // **[class.local]: a class defined in a function body belongs to that function**,
    // so its tag carries the function's name, with a counter where two overloads
    // write the same one. A specialization is not local even where a function asked.
    bool inNamespace = false;
    if ((within == nullptr || introduced) && !tag.empty() && !namespaceStack_.empty() &&
        currentFunction_.empty() && specializationOf.empty()) {
        local = tag;
        tag = namespacePrefix() + tag;
        inNamespace = true;
    }

    std::string localOwner;
    if (within == nullptr && defining && !tag.empty() &&
        !currentFunction_.empty() && specializationOf.empty()) {
        localOwner = currentFunction_;
        std::string qualified = currentFunctionName_ + "::" + tag;
        for (int n = 2; ; n++) {
            auto had = localClassOwner_.find(qualified);
            if (had == localClassOwner_.end() || had->second == localOwner) break;
            qualified = currentFunctionName_ + "$" + std::to_string(n) + "::" + tag;
        }
        local = tag;
        tag = qualified;
    }

    Type *type = tag.empty() ? types_.anonymousStruct(kind)
                             : types_.structType(kind, tag);
    if (!specializationOf.empty()) {
        type->setLocalName(tag);
        type->setSpecialization(specializationOf, instantiatingArgs_);
        type->setTemplateNamespace(instantiatingNamespace_);
        // **Injected class name**: `Holder` in its body is this specialization
        declareTypeName(tag + "::" + specializationOf, type);
    }
    // A class in a namespace: the manglers want the name without the
    // namespaces, and there is no enclosing type to point at - a namespace is
    // not a Type, which is the whole reason its scopes ride in the tag.
    if (inNamespace) { type->setLocalName(local); type->setInNamespace(); }
    if (within != nullptr && !introduced && !local.empty()) {
        type->setLocalName(local);
        type->setEnclosing(within);
    }
    // A definition decides this, and a declaration answers when no definition
    // has.
    if (peek().is("{") || peek().is(":")) type->setDeclaredClass(isClass);
    else                                  type->noteClassKey(isClass);
    if (classFinal) type->setFinal();
    if (!localOwner.empty()) {
        // The single component is what both ABIs spell inside the wrapper, and the
        // written name is what resolves inside this function - which also shadows a
        // global class of the same name, the local scope being asked first.
        type->setLocalName(local);
        type->setLocalOwner(localOwner);
        localClassOwner_[tag] = localOwner;
        if (currentClass_ != nullptr) localClassScope_[tag] = currentClass_;
        localTypes_[local] = type;
    }
    if (!tag.empty()) declareTypeName(tag, type);

    // **[temp.inst]/1: a member class of a specialization being instantiated is declared, not defined** - its
    // tokens and this instantiation's bindings are kept for realizeMemberClass, and its body stepped over.
    if (defining && within != nullptr && within->isSpecialization() && !within->isComplete() &&
        !instantiatingParams_.empty() && specializationOf.empty() && !introduced && localOwner.empty() &&
        !tag.empty() && !anonProper && realizingMember_ != tag && !type->isComplete()) {
        std::size_t close = at_;
        while (close < tokens_.size() && tokens_[close].kind != TokenKind::End && !tokens_[close].is("{")) close++;
        for (int depth = 0; close < tokens_.size() && tokens_[close].kind != TokenKind::End; close++) {
            if (tokens_[close].is("{")) depth++;
            else if (tokens_[close].is("}") && --depth == 0) break;
        }
        if (close + 1 < tokens_.size() && tokens_[close + 1].is(";")) {
            LazyMemberClass lazy;
            lazy.start = headAt;
            lazy.kind = kind;
            lazy.isClass = isClass;
            lazy.owner = within;
            lazy.params = instantiatingParams_;
            lazy.binding = instantiatingBinding_;
            lazy.values = instantiatingValues_;
            lazy.packs = instantiatingPacks();
            lazy.namespaces = namespaceStack_;
            lazyMemberClasses_[tag] = lazy;
            at_ = close + 1;
            return type;
        }
    }

    // `class Derived : public Base {` - the base-clause, which may name more than
    // one. Default access is private for a class and public for a struct. Bases are
    // laid down in the order written, which is the order their constructors run in.
    struct WrittenBase { const Type *type; Access access; bool isVirtual; };
    std::vector<WrittenBase> written;
    if (peek().is(":")) {
        at_++;
        for (;;) {
            Access how = isClass ? Access::Private : Access::Public;
            // [class.derived]/1 lets `virtual` and the access specifier be
            // written in either order.
            bool isVirtualBase = false;
            if (peek().is("virtual")) { isVirtualBase = true; at_++; }
            if (peek().is("public"))         { how = Access::Public;    at_++; }
            else if (peek().is("protected")) { how = Access::Protected; at_++; }
            else if (peek().is("private"))   { how = Access::Private;   at_++; }
            if (!isVirtualBase && peek().is("virtual")) { isVirtualBase = true; at_++; }

            // `: Bs...`, `: Base<Ts>...` - [temp.variadic]/4: one base per member of the pack, each read from a
            // copy of the pattern with the pack bound to that member.
            std::size_t packEnd = 0, members = 0;
            {
                int depth = 0;
                std::size_t e = at_;
                for (; e < tokens_.size() && tokens_[e].kind != TokenKind::End; e++) {
                    const Token &t = tokens_[e];
                    if (t.is("(") || t.is("[") || t.is("<")) depth++;
                    else if (t.is(")") || t.is("]") || t.is(">")) depth--;
                    else if (t.is(">>")) depth -= 2;
                    else if (depth <= 0 && (t.is(",") || t.is("{"))) break;
                }
                bool any = false;
                for (std::size_t i = at_; e > at_ + 1 && tokens_[e - 1].is("...") && i + 1 < e; i++) {
                    const auto pk = tokens_[i].kind == TokenKind::Ident ? packs_.find(tokens_[i].text) : packs_.end();
                    if (pk == packs_.end()) continue;
                    if (any && pk->second.types.size() != members)
                        src_.fail(tokens_[e - 1].pos, "the packs expanded by this '...' have different lengths");
                    members = pk->second.types.size();
                    any = true;
                }
                if (any) packEnd = e;
            }
            auto readBase = [&]() {
            std::size_t bpos = peek().pos;
            const std::size_t bfrom = at_;
            // **Read the base as a type rather than as a name.** A base may be
            // written `A<T>`, which findTypedef cannot answer; specifiers() already
            // knows how to turn one into a type, and a pattern is never parsed.
            std::string baseName = peek().kind == TokenKind::Ident
                                       ? peek().text : std::string();
            const Type *b = nullptr;
            if (atTypeName()) {
                StorageClass bsc;
                Qualifiers bquals;
                b = specifiers(&bsc, &bquals);
                if (b != nullptr) b = b->unqualified();
            } else {
                baseName = expectIdent("a base class name");
            }
            // [temp.variadic]/5: a pack of bases is written with the '...' that expands it.
            if (packEnd == 0 && !baseName.empty() && packs_.count(baseName) != 0)
                src_.fail(bpos, "'" + baseName + "' is a parameter pack, and here it is written without the "
                                "'...' that expands it");
            if (b == nullptr || !b->isStructOrUnion())
                src_.fail(bpos, "'" + baseName + "' is not a class, so it "
                                "cannot be a base");
            realizeMemberClass(b, true);
            if (!b->isComplete())
                src_.fail(bpos, "'" + baseName + "' is not defined yet - a base "
                                "class has to be complete, because the derived "
                                "object contains one");
            if (b->isFinal())
                src_.fail(bpos, "'" + baseName + "' is marked 'final' and cannot "
                                "be a base class - [class]/3");
            // [class.union]/2: a union is never a base, and has none. [class.mi]/3: a class is a direct base once.
            if (b->kind() == Kind::Union)
                src_.fail(bpos, "'" + baseName + "' is a union, and a union cannot be a base class - "
                                "[class.union]/2");
            if (kind == Kind::Union)
                src_.fail(bpos, std::string("a union cannot have a base class - [class.union]/2"));
            for (const WrittenBase &had : written)
                if (had.type == b)
                    src_.fail(bpos, "'" + baseName + "' is named twice as a direct base class - [class.mi]/3");
            written.push_back(WrittenBase{ b, how, isVirtualBase });
            // [temp.dep]/3: a base written with a template parameter is not searched by unqualified lookup.
            for (std::size_t k = bfrom; k < at_ && !instantiatingParams_.empty(); k++)
                for (const TemplateParam &p : instantiatingParams_)
                    if (tokens_[k].kind == TokenKind::Ident && tokens_[k].text == p.name &&
                        std::find(dependentBases_[type].begin(), dependentBases_[type].end(), b) ==
                            dependentBases_[type].end())
                        dependentBases_[type].push_back(b);
            };
            if (packEnd != 0) {
                const std::size_t from = at_;
                for (std::size_t m = 0; m < members; m++) {
                    std::vector<Shadow> undo;
                    struct Unbind {
                        Parser *p; std::vector<Shadow> *u;
                        ~Unbind() { p->unbindTemplateParameters(*u); }
                    } unbind{ this, &undo };
                    bindPatternMember(from, packEnd, m, &undo);
                    readBase();
                    if (peek().kind != TokenKind::End)
                        src_.fail(peek().pos, "this pack expansion's pattern is not one base class");
                }
                at_ = packEnd;
            } else {
                readBase();
            }
            if (!consume(",")) break;
        }
    }
    const Type *base = written.empty() ? nullptr : written[0].type;

    if (!peek().is("{")) {
        if (tag.empty()) src_.fail(pos, std::string(what) + " needs a tag or a body");
        return type;
    }
    at_++;

    if (type->isComplete())
        src_.fail(pos, std::string(what) + " " + tag + " is defined twice");

    // From here to the '}' this class is the innermost scope, which is what makes `Inner` inside it mean `Outer::Inner`.
    classStack_.push_back(type);

    std::vector<Member> members;
    int widest = 1;
    long long bitCursor = 0;
    // **The Microsoft ABI allocates bitfields in units of the declared type**, and
    // starts a new unit when the type changes or the unit fills, where Itanium packs
    // them end to end. These two track the open unit; zero means none is open.
    const bool msBits = target_.microsoftNames();
    long long msUnitStart = 0, msUnitBits = 0;
    long long widestBits = 0;
    // [class.union]/4: at most one member of a union has a brace-or-equal-initializer.
    std::size_t unionInits = 0;

    // **The base subobject is laid down first, at offset 0**, its members
    // copied in at the offsets they already have - that is what the layout IS,
    // so `d.b` needs no second search. Access travels through the inheritance.
    const Type *primary = nullptr;
    if (!target_.microsoftNames())
        for (std::size_t bi = 0; bi < written.size() && primary == nullptr; bi++)
            if (!written[bi].isVirtual && written[bi].type->hasVptr())
                primary = written[bi].type;
    type->setPrimaryBase(primary);
    // **cl lays every base with a vfptr before the rest**, in the order written - `X : E, I1, I2` has
    // I1 at 0, I2 at 16, E at 32 (Phase 6, measured) - and the first of them lends the class its table.
    const bool msOrder = target_.microsoftNames();
    std::vector<std::size_t> layOrder;
    for (std::size_t bi = 0; bi < written.size(); bi++)
        if (!written[bi].isVirtual && (msOrder ? written[bi].type->polymorphic() : written[bi].type == primary))
            layOrder.push_back(bi);
    for (std::size_t bi = 0; bi < written.size(); bi++)
        if (!written[bi].isVirtual && (msOrder ? !written[bi].type->polymorphic() : written[bi].type != primary))
            layOrder.push_back(bi);
    std::vector<int> baseAt(written.size(), 0);
    // cl's two flags, carried by the last base or class-typed member laid and
    // by the first base - see Type::endsWithZeroSized.
    bool endsZero = false, leadsZero = false, leadsKnown = false;
    const Type *prevBase = nullptr;
    // Itanium's conflict list for this class, and the size an empty base pushed
    // past the cursor still claims - `struct CC : E1, C1 { int x; }` has C1 at 1.
    std::vector<Type::EmptyAt> empties;
    long long sizeFloor = 0;
    struct Conflict {
        static bool at(const std::vector<Type::EmptyAt> &have, const Type *t, int off) {
            const std::vector<Type::EmptyAt> &in = t->unqualified()->emptySubobjects();
            for (std::size_t i = 0; i < in.size(); i++)
                for (std::size_t j = 0; j < have.size(); j++)
                    if (have[j].type == in[i].type && have[j].offset == in[i].offset + off)
                        return true;
            return false;
        }
        static void add(std::vector<Type::EmptyAt> &have, const Type *t, int off) {
            const std::vector<Type::EmptyAt> &in = t->unqualified()->emptySubobjects();
            for (std::size_t i = 0; i < in.size(); i++)
                have.push_back(Type::EmptyAt{ in[i].type, in[i].offset + off });
        }
    };

    std::size_t membersFromBases = 0;
    for (std::size_t li = 0; li < layOrder.size(); li++) {
        // A virtual base is not in that order at all: it goes after every
        // non-virtual byte, which is not known until the members have been read.
        const std::size_t bi = layOrder[li];
        const Type *b = written[bi].type;
        const Access how = written[bi].access;

        // Each base starts where the last one's data ended, aligned to its own requirement.
        long long byteCursor = (bitCursor + 7) / 8;
        // **On Microsoft a base that leads with a zero-sized subobject goes a byte
        // past one that ends with one** - cl lays `struct A : E1, E2 { int x; }`
        // with E2 at 1 and x at 4, where Itanium puts both empties at 0.
        if (target_.microsoftNames() && prevBase != nullptr &&
            prevBase->endsWithZeroSized() && b->leadsWithZeroSized())
            byteCursor++;
        // **The same refusal the member list gets, one base earlier.** The sum of the
        // bases is measured in `long long` where `at` is an `int`, so a third huge
        // base wrapped it negative and every later number came from the wreck.
        const int balign = Packed::to(b->align(target_), pack);
        const long long basesEnd = (byteCursor + balign - 1) / balign * balign +
                                   b->dataSize();
        if (basesEnd > 2147483647LL)
            src_.fail(pos, std::string("this ") + what + " is larger than this "
                           "compiler can lay out: its bases need " +
                           std::to_string(basesEnd) + " bytes, past the "
                           "2147483647 an object here is measured in");
        int at = static_cast<int>(alignTo(static_cast<int>(byteCursor), balign));
        // **Itanium puts an empty base at 0**, and only where an empty subobject of
        // the same type is there already does it go to the cursor and on, by its
        // alignment, until nothing of its type is under it. It claims no data.
        if (!target_.microsoftNames()) {
            const bool emptyBase = b->dataSize() == 0 && !b->hasVptr();
            if (emptyBase && !Conflict::at(empties, b, 0)) at = 0;
            else while (Conflict::at(empties, b, at)) at += balign;
            if (emptyBase) {
                if (at + b->size(target_) > sizeFloor) sizeFloor = at + b->size(target_);
                Conflict::add(empties, b, at);
                baseAt[bi] = at;
                if (balign > widest) widest = balign;
                prevBase = b;
                continue;
            }
        }
        Conflict::add(empties, b, at);

        const std::vector<Member> &inherited = b->members();
        for (std::size_t i = 0; i < inherited.size(); i++) {
            Member m = inherited[i];
            m.offset += at;
            // Whose it is, kept through the flattening: a base's private
            // member stays the base's for the access check, however many
            // classes down it is copied.
            if (b->hasVirtualBase() && m.offset >= b->nvDataSize() + at) continue;
            if (m.declaredIn == nullptr) m.declaredIn = b;
            if (m.access == Access::Private) m.access = Access::Private;
            else if (how == Access::Private) m.access = Access::Private;
            else if (how == Access::Protected) m.access = Access::Protected;
            // The same name already copied down from an earlier base: both
            // are ambiguous in this class - [class.member.lookup] - though a
            // member the class writes itself, added after, hides them both.
            for (std::size_t j = 0; j < membersFromBases; j++)
                if (members[j].name == m.name) { members[j].ambiguous = true; m.ambiguous = true; }
            members.push_back(m);
        }
        membersFromBases = members.size();
        baseAt[bi] = at;
        endsZero = b->endsWithZeroSized();
        if (!leadsKnown) { leadsZero = b->leadsWithZeroSized(); leadsKnown = true; }
        prevBase = b;

        // The base's DATA size, not its sizeof - see Type::dataSize.
        bitCursor = static_cast<long long>(at + b->nvDataSize()) * 8;
        if (balign > widest) widest = balign;
        // The base at offset 0 hands its slots down in order, and an override
        // in this class replaces one rather than appending - declareMember
        // does that.
        const bool atZero = primary != nullptr ? b == primary : msOrder ? li == 0 : bi == 0;
        if (!tag.empty() && atZero && b->polymorphic()) {
            vtables_[tag] = vtables_[b->tag()];
            ownSlotsFrom_[tag] = vtables_[tag].size();
        }
    }
    // Recorded in the order written, whatever order they were laid in: the
    // constructor and the destructor walk this list.
    for (std::size_t bi = 0; bi < written.size(); bi++)
        if (!written[bi].isVirtual)
            type->addBase(written[bi].type, baseAt[bi], written[bi].access,
                          false, true, static_cast<int>(bi));
    const std::size_t nonVirtualBases = type->bases().size();
    // **The virtual bases, one each, and the set is transitive.**
    struct Gather {
        // `Dia : D1, D2` writes no `virtual` yet lays V down, "most derived"
        // being about the object; and the walk is clang's preorder: each
        // direct base as written, a virtual one as met, then those behind it.
        static void of(const Type *t, std::vector<const Type *> &out,
                       std::vector<Access> &how, Access through) {
            if (!t->hasVirtualBase()) return;     // nothing behind it, and a chain of bases asks at every class
            const std::vector<const Type::BaseSpec *> bs = t->directBases();
            for (std::size_t i = 0; i < bs.size(); i++) {
                Access a = (bs[i]->access == Access::Private ||
                            through == Access::Private) ? Access::Private
                         : (bs[i]->access == Access::Protected ||
                            through == Access::Protected) ? Access::Protected
                                                          : Access::Public;
                if (bs[i]->isVirtual) {
                    bool seen = false;
                    for (std::size_t k = 0; k < out.size(); k++)
                        if (out[k] == bs[i]->type) seen = true;
                    if (!seen) { out.push_back(bs[i]->type); how.push_back(a); }
                }
                of(bs[i]->type, out, how, a);
            }
        }
    };
    std::vector<const Type *> vbases;
    std::vector<Access> vaccess;
    for (std::size_t bi = 0; bi < written.size(); bi++) {
        if (written[bi].isVirtual) {
            bool seen = false;
            for (std::size_t k = 0; k < vbases.size(); k++)
                if (vbases[k] == written[bi].type) seen = true;
            if (!seen) { vbases.push_back(written[bi].type);
                         vaccess.push_back(written[bi].access); }
        }
        Gather::of(written[bi].type, vbases, vaccess, written[bi].access);
    }
    // The ones this class *wrote* `virtual` for keep the access it wrote and
    // are its direct bases; the rest are laid down here all the same but
    // are not - see BaseSpec::direct.
    std::vector<int> vwritten(vbases.size(), -1);
    for (std::size_t bi = 0; bi < written.size(); bi++)
        if (written[bi].isVirtual)
            for (std::size_t k = 0; k < vbases.size(); k++)
                if (vbases[k] == written[bi].type && vwritten[k] < 0) {
                    vwritten[k] = static_cast<int>(bi);
                    vaccess[k] = written[bi].access;
                }

    // **Recorded now, at no offset yet**, so that a member function declared
    // below is seen to override one of theirs; where each sits is settled
    // once the non-virtual part is measured.
    for (std::size_t bi = 0; bi < vbases.size(); bi++)
        type->addBase(vbases[bi], 0, vaccess[bi], true, vwritten[bi] >= 0,
                      vwritten[bi]);


    // **A vptr sits at offset 0**, so the members start after it - measured.
    const bool inheritsVptr = primary != nullptr;

    // **The Microsoft vbtable pointer goes after the bases**, not in front of them: cl lays `N : A,
    // virtual V { int n; }` as A 0, vbptr 8, n 16, V 24, and `Q : A, virtual V { int q; virtual
    // void h(); }` as vfptr 0, A 8, vbptr 16, q 24.
    int msVbptrAt = -1;
    if (target_.microsoftNames() && kind != Kind::Union) {
        bool wantsVbptr = false, carried = false;
        for (std::size_t bi = 0; bi < written.size(); bi++) {
            if (written[bi].isVirtual) { wantsVbptr = true; continue; }
            if (written[bi].type->hasVirtualBase()) wantsVbptr = true;
            if (written[bi].type->vbptrOffset() >= 0) carried = true;
        }
        if (wantsVbptr && !carried) {
            msVbptrAt = alignTo(static_cast<int>((bitCursor + 7) / 8), 8);
            bitCursor = static_cast<long long>(msVbptrAt + 8) * 8;
            if (widest < 8) widest = 8;
        }
    }

    // **The one difference between the two keywords.**
    Access access = isClass ? Access::Private : Access::Public;

    // **Where this class's own members begin.**
    const std::size_t ownFrom = members.size();

    while (!peek().is("}")) {
        const ReadingAt reading(this, &memberAt_);
        if (consume(";")) continue;     // a member-declaration of nothing, which [class.mem]'s grammar allows
        if (peek().kind == TokenKind::End) src_.fail(pos, "unclosed '{'");

        // **A member function template.**
        if (peek().is("template")) {
            if (tag.empty())
                src_.fail(peek().pos, "a member template needs a named class");
            TemplateDecl mt;
            mt.isMember = true;
            mt.start = at_;
            at_++;
            if (!peek().is("<"))
                src_.fail(peek().pos, "explicit instantiation in a class is not "
                                      "supported yet");
            templateParameters(mt.params);
            // [temp.param]/11: a member function template's pack may be followed by a parameter with a default -
            // `template <class U, class... V, class = enable_if<...>>`; a member class template's by none (below).
            {
                bool after = false, defaulted = true;
                for (const TemplateParam &q : mt.params) {
                    if (after && q.defBegin == 0 && !q.isPack) defaulted = false;
                    after = after || q.isPack;
                }
                if (!defaulted) refuseAfterPack(mt.params);
            }
            // `template <class R> constexpr explicit duration(const R &)` is read as `explicit constexpr`, as a
            // member that is no template is below.
            if (peek().is("constexpr") && peekAt(1).is("explicit")) std::swap(tokens_[at_], tokens_[at_ + 1]);
            mt.afterParams = at_;
            mt.pos = peek().pos;
            // **A friend template** - [temp.friend]/1 (R12): `template <class U> friend class H;`
            // befriends every specialization of H, `template <class T> friend int tpeek(const B &);`
            // every one of tpeek.
            if (peek().is("friend")) {
                at_++;
                if (peek().is("class") || peek().is("struct")) {
                    at_++;
                    friendClasses_[tag].push_back("template " + expectIdent("the class template to befriend"));
                    expect(";");
                    continue;
                }
                std::string fname;
                const std::size_t friendFrom = at_;
                int fdepth = 0;
                for (std::size_t i = at_; i < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
                    // `operator/`, `operator()`: an operator's name is the keyword and what follows it.
                    if (fdepth == 0 && tokens_[i].is("operator")) {
                        fname = "operator";
                        if (tokens_[i + 1].is("(") && tokens_[i + 2].is(")")) fname += "()";
                        else if (tokens_[i + 1].is("[") && tokens_[i + 2].is("]")) fname += "[]";
                        else fname += tokens_[i + 1].text;
                        break;
                    }
                    if (fdepth == 0 && tokens_[i].is("(")) break;
                    if (tokens_[i].is("<")) fdepth++;
                    else if (tokens_[i].is(">")) fdepth--;
                    else if (fdepth == 0 && tokens_[i].kind == TokenKind::Ident) fname = tokens_[i].text;
                }
                if (fname.empty()) src_.fail(mt.pos, "this friend template names nothing to befriend");
                bool friendBody = false;
                for (int depth = 0; peek().kind != TokenKind::End && !(depth == 0 && peek().is(";")); at_++) {
                    if (depth == 0 && peek().is("{")) { friendBody = true; break; }
                    if (peek().is("(")) depth++;
                    else if (peek().is(")")) depth--;
                }
                friends_[tag].push_back("template " + fname);
                if (!friendBody) { expect(";"); continue; }
                // **Defined here**: a function template of the class's namespace, found by argument-dependent
                // lookup ([namespace.memdef]/3), read in this class with a class template's parameters bound as
                // this specialization has them ([temp.friend], [class.friend]/6-7; 1.82: "not supported yet").
                TemplateDecl fd;
                fd.params = mt.params;
                fd.start = mt.start;
                fd.afterParams = friendFrom;
                fd.pos = mt.pos;
                fd.name = fname;
                fd.ns = namespacePrefix();
                fd.isClass = false;
                fd.defined = true;
                fd.friendOf = tag;
                fd.classParams = instantiatingParams_;
                fd.classBinding = instantiatingBinding_;
                fd.classValues = instantiatingValues_;
                fd.classPacks = instantiatingPacks();
                fnTemplates_[fname].push_back(fd);
                if (templates_.find(fname) == templates_.end()) templates_[fname] = fd;
                skipBracedBlock();
                continue;
            }
            // **A member alias template** - [temp.alias] inside a class (R12), named through it: `S::M<int>`.
            // A class template's keeps the class's arguments, bound again wherever it is used.
            if (peek().is("using")) {
                TemplateDecl alias;
                alias.params = mt.params;
                alias.start = mt.start;
                alias.pos = mt.pos;
                alias.ownerTag = tag;
                alias.ownerLocal = local;
                alias.memberAccess = access;
                alias.classParams = instantiatingParams_;
                alias.classBinding = instantiatingBinding_;
                alias.classValues = instantiatingValues_;
                alias.classPacks = instantiatingPacks();
                at_++;
                alias.name = tag + "::" + expectIdent("the alias template's name");
                alias.afterParams = mt.afterParams;
                expect("=");
                alias.aliasAt = at_;
                alias.isAlias = alias.isClass = alias.defined = true;
                alias.ns = namespacePrefix();
                if (templates_.count(alias.name) != 0 && instantiatingParams_.empty())
                    src_.fail(alias.pos, "'" + alias.name + "' is already a template");
                for (int depth = 0; peek().kind != TokenKind::End && !(depth == 0 && peek().is(";")); at_++) {
                    if (peek().is("(") || peek().is("{")) depth++;
                    else if (peek().is(")") || peek().is("}")) depth--;
                }
                expect(";");
                templates_[alias.name] = alias;
                continue;
            }
            // **A member class template** - [temp.mem]/1, `allocator::rebind`: kept as the alias is, and
            // instantiated as a class nested in this one.
            if (peek().is("struct") || peek().is("class") || peek().is("union")) {
                if (!type->localOwner().empty())
                    src_.fail(mt.pos, "a local class cannot have a member template - [temp.mem]/2");
                refuseAfterPack(mt.params);
                TemplateDecl mc;
                mc.params = mt.params;
                mc.start = mt.start;
                mc.afterParams = mt.afterParams;
                mc.pos = mt.pos;
                mc.isClass = true;
                mc.classKey = peek().is("class");
                mc.ns = namespacePrefix();
                mc.ownerTag = tag;
                mc.ownerLocal = local;
                mc.memberAccess = access;
                mc.classParams = instantiatingParams_;
                mc.classBinding = instantiatingBinding_;
                mc.classValues = instantiatingValues_;
                mc.classPacks = instantiatingPacks();
                at_++;
                skipAttributes();
                const std::string mcName = expectIdent("the member class template's name");
                if (peek().is("<"))
                    src_.fail(peek().pos, "a partial specialization of a member class template is not "
                                          "supported yet");
                mc.name = tag + "::" + mcName;
                at_ = mc.afterParams;
                mc.defined = skipTemplatedDefinition();
                if (!mc.defined)
                    src_.fail(mc.pos, "a member class template declared here and defined outside its class "
                                      "is not supported yet - define it inside");
                if (templates_.count(mc.name) != 0 && instantiatingParams_.empty())
                    src_.fail(mc.pos, "'" + mc.name + "' is already a template");
                templates_[mc.name] = mc;
                continue;
            }
            // The member's name: the identifier at depth 0 immediately before
            // the parameter list's '(' - the return type's own '<...>' is nested.
            std::string mname;
            int depth = 0;
            for (std::size_t i = at_; i + 1 < tokens_.size(); i++) {
                const Token &t = tokens_[i];
                // **An operator's name is not one token**, and it is spelled
                // before the parameter list rather than being an identifier.
                if (depth == 0 && t.is("operator")) {
                    if (tokens_[i + 1].is("(") && i + 2 < tokens_.size() &&
                        tokens_[i + 2].is(")"))
                        mname = "operator()";
                    else if (tokens_[i + 1].is("[") && i + 2 < tokens_.size() &&
                             tokens_[i + 2].is("]"))
                        mname = "operator[]";
                    // `template <class T> operator T *()`: a conversion's name is the type it
                    // deduces, so every such one is kept under one key (R6).
                    else if (tokens_[i + 1].kind != TokenKind::Punct &&
                             tokens_[i + 1].kind != TokenKind::Str &&
                             !tokens_[i + 1].is("new") && !tokens_[i + 1].is("delete"))
                        mname = conversionTemplateKey();
                    else
                        mname = "operator" + tokens_[i + 1].text;
                    break;
                }
                if (depth == 0 && t.is("(")) {
                    // `static void (*f())(void *)`: a declarator in parentheses, whose name is the identifier
                    // its own parameter list follows (1.82: "a member function template needs a name").
                    if (tokens_[i + 1].is("*") || tokens_[i + 1].is("&") || tokens_[i + 1].is("&&")) {
                        int inner = 0;
                        for (std::size_t j = i; j + 1 < tokens_.size(); j++) {
                            if (tokens_[j].is("(")) inner++;
                            else if (tokens_[j].is(")")) { if (--inner == 0) break; }
                            else if (tokens_[j].kind == TokenKind::Ident && tokens_[j + 1].is("(")) {
                                mname = tokens_[j].text;
                                break;
                            }
                        }
                    }
                    break;
                }
                if (depth == 0 && t.is(";")) break;
                if (t.is("<") || t.is("(") || t.is("[")) depth++;
                else if (t.is(">") || t.is(")") || t.is("]")) depth--;
                else if (depth == 0 && t.kind == TokenKind::Ident) mname = t.text;
            }
            if (mname.empty())
                src_.fail(mt.pos, "a member function template needs a name");
            // [temp.mem]/2-3: no member template in a local class, and none that is virtual or a destructor.
            if (!type->localOwner().empty())
                src_.fail(mt.pos, "a local class cannot have a member template - [temp.mem]/2");
            for (std::size_t i = at_, depth = 0; i < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
                const Token &t = tokens_[i];
                if (depth == 0 && (t.is("(") || t.is(";"))) break;
                if (t.is("<") || t.is("[")) depth++;
                else if ((t.is(">") || t.is("]")) && depth > 0) depth--;
                else if (depth == 0 && t.is("virtual"))
                    src_.fail(t.pos, "a member function template cannot be virtual - [temp.mem]/3");
                else if (depth == 0 && t.is("~"))
                    src_.fail(t.pos, "a destructor cannot be a member template - [temp.mem]/2");
            }
            // **A constructor template is named by the class's own name** - `template <size_t K>
            // CMatrix<M, N>(const CVector<K> &)` - and keyed with the other constructors.
            if (mname == local) {
                mt.isConstructor = true;
                mname = localOf(tag);
            }
            mt.name = mname;
            mt.ownerLocal = local;
            mt.memberAccess = access;
            mt.ownerTag = tag;
            mt.ownerType = nullptr;    // filled at instantiation from the tag
            // Its signature names its namespace's types from a call anywhere: `Q(Tag2, A)` in N, called from main.
            mt.ns = namespacePrefix();
            // A member of a class template: capture the class's own parameters
            // and this instantiation's binding, so both layers bind at replay.
            mt.classParams = instantiatingParams_;
            mt.classBinding = instantiatingBinding_;
            mt.classValues = instantiatingValues_;
            mt.classPacks = instantiatingPacks();
            at_ = mt.afterParams;
            mt.defined = skipTemplatedDefinition();
            // [temp.mem]/1: declared here and defined outside - or nowhere, the sizeof idiom's `test` -
            // a declaration until its definition is read (outOfLineMemberTemplate).
            if (!mt.defined) applyOutOfLineMemberTemplate(&mt);
            memberTemplates_[tag + "::" + mname].push_back(mt);
            continue;
        }

        // **A using-declaration in a class is a different rule from one at namespace scope**, which
        // this compiler has: here it redeclares a base member, changing its access or bringing an
        // overload set into the derived class's own, and neither is an alias.
        if (peek().is("using") && tag.empty() && peekAt(2).is("="))
            src_.fail(peek().pos, "an alias declaration needs a class with a name - this one is anonymous");
        const std::string aliasName = peekAt(1).text;
        if (aliasDeclaration(tag + "::")) {
            memberTypeAccess_[tag + "::" + aliasName] = access;
            continue;
        }
        // **`using B::B;` inherits B's constructors**, [class.inhctor] - recorded here, declared once the class is complete.
        if (peek().is("using") && !tag.empty()) {
            std::size_t e = at_ + 1;
            for (int depth = 0; e < tokens_.size() && !(depth == 0 && tokens_[e].is(";")); e++) {
                if (tokens_[e].is("<")) depth++;
                else if (tokens_[e].is(">")) depth--;
                else if (tokens_[e].kind == TokenKind::End) break;
            }
            const Type *named = nullptr;
            if (e < tokens_.size() && e >= at_ + 4 && tokens_[e - 1].kind == TokenKind::Ident &&
                tokens_[e - 2].is("::")) {
                const std::string ctor = tokens_[e - 1].text;
                for (const Type::BaseSpec *b : type->directBases())
                    if (b->type->localName() == ctor ||
                        (b->type->isSpecialization() && b->type->templateName() == ctor))
                        named = b->type;
            }
            if (named != nullptr) {
                for (const Type::BaseSpec *b : type->directBases())
                    if (b->type == named && b->isVirtual)
                        src_.fail(peek().pos, "inheriting the constructors of a virtual base is not "
                                              "supported yet - the most-derived class would have to be told "
                                              "which of them builds it");
                inheritFrom_[tag].push_back(named);
                at_ = e + 1;
                continue;
            }
        }
        if (peek().is("using")) {
            classUsingDeclaration(tag, type, access);
            continue;
        }

        // Where this member's declaration begins.
        std::size_t itemStart = at_;

        // Set when a body was held and stepped over.
        bool heldBody = false;

        // **`explicit`: the replay must not start at it** (as `virtual`): an
        // out-of-line re-read does not write it. `constexpr explicit` is put
        // in the order read here, for every replay of these tokens.
        if (peek().is("constexpr") && peekAt(1).is("explicit")) std::swap(tokens_[at_], tokens_[at_ + 1]);
        bool isExplicit = false;
        const std::size_t explicitAt = peek().pos;
        if (peek().is("explicit")) { isExplicit = true; at_++; itemStart = at_; }

        bool constexprCtorAhead = false;
        // **A `constexpr` constructor makes a literal type** - one whose
        // objects a constant expression may build.
        if (!tag.empty() && peek().is("constexpr") &&
            peekAt(1).kind == TokenKind::Ident && peekAt(1).text == local &&
            peekAt(2).is("(")) {
            // [dcl.constexpr]/3: one may always run at run time, so it is an ordinary constructor here,
            // and its body is kept where it is defined for a constant object to be built by.
            refuseConstexprConstructorBody(2);
            at_++;
            itemStart = at_;
            constexprCtorAhead = true;
        }

        // A constructor has the class's own name and no return type, so it has to be seen
        // before specifiers() is asked for one - the name is a registered type name by now
        // and would be read as the type. `V<N>(` is the same, named with its own arguments.
        const std::size_t namedAt = peek().pos;
        const bool ownId = !tag.empty() && atOwnTemplateId(tag, local);
        if (ownId || (!tag.empty() && peek().kind == TokenKind::Ident &&
                      peek().text == local && peekAt(1).is("("))) {
            std::size_t cpos = namedAt;
            if (!ownId) at_++;
            const std::size_t sigAt = functions_.size();
            declareConstructor(tag, cpos, access, isExplicit);
            if (constexprCtorAhead && functions_.size() > sigAt) constexprCtorSymbols_.insert(functions_[sigAt].symbol);
            // [dcl.constexpr]/4: the definition's parameter types are literal types.
            if (constexprCtorAhead && functions_.size() > sigAt && !type->isSpecialization() &&
                (peek().is("{") || peek().is(":") || peek().is("try")))
                for (const Type *p : functions_[sigAt].params)
                    if (plainlyNotLiteral(p))
                        src_.fail(cpos, "this 'constexpr' constructor's parameter type '" + p->describe() +
                                        "' is not a literal type - [dcl.constexpr]/4");
            isExplicit = false;
            refuseVirtSpecifiers("a constructor");
            if (peek().is("{") || peek().is(":") || peek().is("try")) {
                pendingBodies_.push_back(PendingBody{
                    tag, itemStart, local, constructorKey(tag),
                    signatureAddedUnder(constructorKey(tag), sigAt) });
                markInlineBody(pendingBodies_.back().which);
                skipBracedBlock();
                continue;
            }
            expect(";");
            continue;
        }

        // **`explicit` on a conversion function is C++11**: carried to the
        // Signature, where conversionFunction leaves it out of copy-init;
        // a specifier may stand between (explicit constexpr operator bool).
        std::size_t opAt = 0;
        while (peekAt(opAt).is("constexpr") || peekAt(opAt).is("inline")) opAt++;
        if (isExplicit && peekAt(opAt).is("operator") &&
            peekAt(opAt + 1).kind != TokenKind::Punct) {
            pendingExplicitConversion_ = true;
            isExplicit = false;
        }

        // Anything else it was written on: neither a constructor, which the
        // branch above read, nor a conversion function.
        if (isExplicit)
            src_.fail(explicitAt, "'explicit' applies to a constructor or a "
                                  "conversion function, and this declaration "
                                  "is neither");

        // **The replay must not start at `virtual`.** A held body is re-read through
        // the ordinary out-of-line path, where the keyword is not written, so it is
        // stepped over and the replay begins at the return type.
        bool isVirtual = false;
        if (peek().is("virtual")) { isVirtual = true; at_++; itemStart = at_; }

        // `~Point();` - a destructor, recognised the same way and for the same
        // reason as a constructor: it has no return type and its name is the
        // class, so specifiers() must not be asked for one.
        const std::size_t tildeAt = peek().pos;
        bool ownDtor = false;
        if (!tag.empty() && peek().is("~")) {
            at_++;
            ownDtor = atOwnTemplateId(tag, local);   // `~V<N>(`, left at the `(`
            if (!ownDtor) at_--;
        }
        if (ownDtor || (!tag.empty() && peek().is("~") &&
                        peekAt(1).kind == TokenKind::Ident &&
                        peekAt(1).text == local && peekAt(2).is("("))) {
            std::size_t dpos = tildeAt;
            if (!ownDtor) at_ += 2;
            const std::size_t sigAt = functions_.size();
            declareDestructor(tag, dpos, access, isVirtual);
            if (peek().is("{") || peek().is("try")) {
                pendingBodies_.push_back(PendingBody{
                    tag, itemStart, local, destructorKey(tag),
                    signatureAddedUnder(destructorKey(tag), sigAt) });
                markInlineBody(pendingBodies_.back().which);
                skipBracedBlock();
                continue;
            }
            expect(";");
            continue;
        }

        if (staticAssertion()) continue;

        if ((peek().is("public") || peek().is("private") || peek().is("protected")) &&
            peekAt(1).is(":")) {
            access = peek().is("public")  ? Access::Public
                   : peek().is("private") ? Access::Private
                                          : Access::Protected;
            at_ += 2;
            continue;
        }

        // `friend int peek(const Account &a);` - a declaration written inside a class
        // that declares nothing in it. **[class.friend]: the function belongs to the
        // enclosing namespace, and what the class gives it is access.**
        if (peek().is("friend")) {
            const std::size_t fpos = peek().pos;
            at_++;
            // **The replay begins after `friend`, not at it** - the same rule `virtual` follows:
            // the keyword is written on the declaration inside the class and nowhere else, so a
            // body replayed from it would hand `specifiers()` a keyword it has no rule for.
            const std::size_t friendStart = at_;
            if (tag.empty())
                src_.fail(fpos, "an anonymous class has no name to grant "
                                "friendship with");
            const bool keyed = peek().is("class") || peek().is("struct") || peek().is("union");
            if (keyed || (peek().kind == TokenKind::Ident && peekAt(1).is(";") &&
                          findTypedef(peek().text) != nullptr &&
                          findTypedef(peek().text)->unqualified()->isStructOrUnion())) {
                befriendClass(tag, keyed);
                continue;
            }
            StorageClass fsc;
            Qualifiers fquals;
            const Type *fbase = specifiers(&fsc, &fquals);
            if (fsc != StorageNone)
                src_.fail(fpos, "a friend declaration takes no storage class - "
                                "the function it names is somebody else's");
            // [class.friend]/3: `friend T;` befriends the class T names, and does nothing where it names none.
            if (consume(";")) {
                if (fbase->unqualified()->isStructOrUnion()) friendClasses_[tag].push_back(fbase->unqualified()->tag());
                continue;
            }
            Declared fd = declarator(fbase);
            // `constexpr` makes no return type const, as the definition's replay reads it (1.82: a constexpr
            // hidden friend returning its class was "declared to return 'const struct P'").
            if (fquals.isConstexpr && fd.type != nullptr && !fd.type->isFunction()) fd.type = types_.withoutConst(fd.type);
            // `friend int B::peek(const A &) const;` - [class.friend]/4: a member of a class declared
            // before, found by its parameters and cv as a definition outside B would find it.
            if (!fd.qualifier.empty()) {
                const Type *other = findTypedef(fd.qualifier);
                if (other == nullptr || !other->unqualified()->isStructOrUnion() || !other->isComplete())
                    src_.fail(fd.pos, "'" + fd.qualifier + "' is not a class defined before this friend "
                                      "declaration, so it has no member '" + fd.name + "' to befriend");
                std::vector<const Type *> fparams;
                bool fvariadic = false;
                parameterTypes(fparams, fvariadic);
                bool fconst = false, fvolatile = false;
                while (peek().is("const") || peek().is("volatile")) {
                    if (consume("const")) fconst = true;
                    else if (consume("volatile")) fvolatile = true;
                }
                const RefQual fref = refQualifier();
                exceptionSpecification();
                const std::string key = other->unqualified()->tag() + "::" + fd.name;
                const Signature *found = nullptr;
                if (const std::vector<std::size_t> *set = overloadsOf(key))
                    for (std::size_t k : *set) {
                        const Signature &f = functions_[k];
                        if (f.owner == other->unqualified()->tag() && f.constThis == fconst &&
                            f.volatileThis == fvolatile && f.refThis == fref && f.variadic == fvariadic &&
                            sameParameters(f.params, fparams))
                            found = &f;
                    }
                if (found == nullptr)
                    src_.fail(fd.pos, "'" + fd.qualifier + "' declares no member '" + fd.name + "' with these "
                                      "parameters and qualifiers for this friend declaration to name");
                friends_[tag].push_back(found->symbol);
                expect(";");
                continue;
            }
            if (!peek().is("("))
                src_.fail(fd.pos, "a friend declaration declares a function, "
                                  "and '" + fd.name + "' is not one - a friend "
                                  "gets access, and only something that runs "
                                  "can use it");
            std::vector<const Type *> fparams;
            bool fvariadic = false;
            parameterTypes(fparams, fvariadic);
            pendingNoexcept_ = exceptionSpecification();
            if (peek().is("const"))
                src_.fail(peek().pos, "'const' here would say the function has "
                                      "a 'this' to leave alone, and a friend "
                                      "is not a member function");
            const bool friendHasBody = peek().is("{");
            const std::size_t fsigAt = functions_.size();
            friendDeclaration_ = true;
            declareFunction(fd.name, fd.type, fparams, fvariadic, false, fd.pos,
                            false);
            friendDeclaration_ = false;
            if (functions_.size() > fsigAt && functions_[fsigAt].pos == fd.pos) functions_[fsigAt].hiddenFriend = true;
            // **A friend defined here is held and replayed as a free
            // function.** [class.friend]/6 makes such a definition implicitly
            // inline, which is what a replay produces anyway.
            if (friendHasBody) {
                PendingBody held{ tag, friendStart, std::string(), fd.name,
                                  signatureAddedUnder(fd.name, fsigAt) };
                held.freeFunction = true;
                held.friendNamespace = namespaceStack_;
                // In a namespace the signature went under the namespace's key, not the bare name.
                if (held.which == PendingBody::npos() && functions_.size() > fsigAt) held.which = fsigAt;
                pendingBodies_.push_back(held);
            }
            // **The grant is to this function, not to its name.** Recording
            // the name would befriend every overload of it, including ones
            // declared later that the class never saw.
            friends_[tag].push_back(
                lookupSignature(fd.name, fparams, fvariadic, fd.pos).symbol);
            // A definition ends at its '}' and has no ';' to consume.
            if (friendHasBody) skipBracedBlock();
            else expect(";");
            continue;
        }

        StorageClass msc;
        Qualifiers mquals;
        const bool wasEnum = peek().is("enum");
        // **`mutable` is a decl-specifier on a non-static data member**, and
        // read here rather than in `specifiers` because it names no type and
        // nothing outside a class body may write it.
        const bool isMutable = consume("mutable");
        const std::size_t enumsBefore = enums_.size();
        const std::size_t specFrom = at_;
        const Type *base = specifiers(&msc, &mquals);
        if (wasEnum)
            for (std::size_t e = enumsBefore; e < enums_.size(); e++) enums_[e].access = access;

        // **A typedef inside a class names a type and declares no member.** It is
        // keyed "S::value", the same qualified key a nested class uses, so it is
        // found from inside the class, from a member's body, and from outside.
        if (msc == StorageTypedef) {
            if (anonProper) {
                const Declared td = declarator(base);
                src_.fail(td.pos, "types cannot be declared in an anonymous union");
            }
            if (tag.empty())
                src_.fail(peek().pos, "a typedef needs a class with a name - "
                                      "this one is anonymous");
            const bool definesClass = typedefDefinesClass(specFrom, at_);
            do {
                Declared td = declarator(base);
                typedefFunctionSuffix(td);
                if (td.name.empty())
                    src_.fail(td.pos, "this typedef names nothing");
                declareTypeName(tag + "::" + td.name, td.type);
                memberTypeAccess_[tag + "::" + td.name] = access;
                if (definesClass && td.type->tag().empty()) td.type->nameForLinkage(tag + "::" + td.name, td.name, type, false);
            } while (consume(","));
            expect(";");
            continue;
        }

        if (msc != StorageNone && msc != StorageStatic)
            src_.fail(peek().pos, "'static' is the only storage class a member "
                                  "may have");

        // **`struct Inner { ... };` declares a type and no member.**
        if (peek().is(";")) {
            // [class.union]/5: a type declared in an anonymous union, and clang's column is the name's.
            if (anonProper && (wasEnum || (base->isStructOrUnion() && !base->tag().empty())))
                src_.fail(tokens_[itemStart + 1].pos, "types cannot be declared in an anonymous union");
            // An `enum Kind { ... };` in a class body declares a type, no member.
            if (wasEnum) { at_++; continue; }
            // **An anonymous union's members are members of the class around it** - [class.union]/5 - at one
            // offset, with the access where it is written; an anonymous struct is the GNU extension clang refuses.
            if (base->isStructOrUnion() && base->tag().empty()) {
                if (base->kind() != Kind::Union)
                    src_.fail(tokens_[itemStart].pos, "anonymous structs are a GNU extension");
                if (anonProper)
                    src_.fail(tokens_[itemStart].pos, "anonymous types declared in an anonymous union are an extension");
                if (msc != StorageNone)
                    src_.fail(tokens_[itemStart].pos, "anonymous union at class scope must not have a storage specifier");
                if (base->members().empty())
                    src_.fail(tokens_[itemStart].pos, "declaration does not declare anything");
                const Type *u = base;
                const int a = Packed::to(u->align(target_), pack);
                if (a > widest) widest = a;
                const long long openEnd = (msBits && msUnitBits != 0) ? msUnitStart + msUnitBits : bitCursor;
                const long long byteCursor = ((bitCursor > openEnd ? bitCursor : openEnd) + 7) / 8;
                long long at = (kind == Kind::Union) ? 0 : alignTo(byteCursor, a);
                // The one slot is laid as a member of the union's type would be, the empty-subobject walk included.
                endsZero = u->endsWithZeroSized();
                if (!target_.microsoftNames() && kind != Kind::Union)
                    while (Conflict::at(empties, u, static_cast<int>(at))) at += a;
                Conflict::add(empties, u, static_cast<int>(at));
                const std::vector<Member> &inner = u->members();
                const std::vector<std::size_t> &where = anonUnionMemberPos_[u];
                for (std::size_t i = 0; i < inner.size(); i++) {
                    const std::size_t mpos = i < where.size() ? where[i] : peek().pos;
                    for (std::size_t j = ownFrom; j < members.size(); j++)
                        if (members[j].name == inner[i].name)
                            src_.fail(mpos, "member of anonymous union redeclares '" + inner[i].name + "'");
                    Member m = inner[i];
                    m.offset += static_cast<int>(at);
                    m.access = access;
                    m.anonUnion = u;
                    members.push_back(m);
                    // Its initialiser was keyed under the nameless tag; it is this class's now.
                    std::map<std::string, std::size_t>::iterator init = memberInit_.find("::" + inner[i].name);
                    if (init != memberInit_.end()) {
                        memberInit_[tag + "::" + inner[i].name] = init->second;
                        memberInit_.erase(init);
                    }
                }
                const long long endBits = (at + u->size(target_)) * 8;
                if (kind == Kind::Union) { if (endBits > widestBits) widestBits = endBits; }
                else bitCursor = endBits;
                msUnitBits = 0;
                at_++;
                continue;
            }
            if (!base->isStructOrUnion())
                src_.fail(peek().pos, "this declares nothing - a member needs a "
                                      "name");
            if (base->enclosing() != nullptr)
                types_.structType(base->kind(), base->tag())
                      ->setNestedAccess(access);
            at_++;
            continue;
        }
        for (;;) {
            if (peek().is(":")) {
                std::size_t cpos = peek().pos;
                at_++;
                long long w = constantExpression("a bit-field width");
                if (!base->isInteger())
                    src_.fail(cpos, "a bit-field must have an integer type, not '" +
                                    base->describe() + "'");
                long long unitBits = base->size(target_) * 8;
                if (w < 0 || w > unitBits)
                    src_.fail(cpos, "a bit-field of " + std::to_string(w) +
                                    " bits does not fit in '" + base->describe() + "'");
                // **A zero-width bitfield does not raise the class's alignment on
                // Itanium**, whatever its type: `{char a; int :0; char b;}` is 5
                // bytes aligned 1 there and 2 aligned 1 on Microsoft.
                int a = Packed::to(base->align(target_), pack);
                if (a > widest && w != 0) widest = a;
                if (w == 0) {
                    // Itanium rounds the cursor to the next unit of this type, which
                    // is what makes the next field start there. **The Microsoft ABI
                    // asks what the `:0` interrupts**: an open unit is charged whole.
                    if (msBits) {
                        if (msUnitBits != 0) {
                            bitCursor = msUnitStart + msUnitBits;
                            msUnitBits = 0;
                            bitCursor = alignTo(bitCursor,
                                                static_cast<long long>(a) * 8);
                            if (a > widest) widest = a;
                        } else {
                            bitCursor = alignTo(bitCursor, 8);
                        }
                    }
                    else bitCursor = alignTo(bitCursor, unitBits);
                } else if (kind != Kind::Union) {
                    if (msBits) {
                        if (msUnitBits != unitBits ||
                            bitCursor - msUnitStart + w > msUnitBits) {
                            const long long full = msUnitBits != 0
                                ? msUnitStart + msUnitBits : bitCursor;
                            const long long byteCursor =
                                ((bitCursor > full ? bitCursor : full) + 7) / 8;
                            msUnitStart = alignTo(byteCursor, a) * 8;
                            msUnitBits = unitBits;
                            bitCursor = msUnitStart;
                        }
                        bitCursor += w;
                    } else {
                        if (bitCursor % unitBits + w > unitBits)
                            bitCursor = alignTo(bitCursor, unitBits);
                        bitCursor += w;
                    }
                }
                if (kind == Kind::Union && w > unitBits) w = unitBits;
                if (kind == Kind::Union && w > widestBits) widestBits = w;
                if (!consume(",")) break;
                continue;
            }

            Declared d = declarator(base);
            if (fnPointerSpec_ == 1) noexceptPointers_.insert(tag + "::" + d.name);

            // A reference member has to be bound when the object is made, so it is
            // refused by name rather than laid out as a pointer. **Only where it is a
            // member at all**: `int &get()` is a function and d.type is its return.
            const bool memberIsFunction = peek().is("(") || d.paramsAt != 0 ||
                                          d.type->isFunction();
            // **`int (*f())(int);` and `int (C::*g())();`**: a member function that returns a pointer to
            // function or to member function has its parameter list inside the declarator's own parentheses.
            // The list is read from there, and the declaration goes on after the whole declarator (1.79: such
            // a member was a data member that then met a body, "expected ';'", or was declared with no
            // parameters, so its definition outside the class matched nothing).
            std::size_t memberResumeAt = 0;
            if (d.paramsAt != 0 && !peek().is("(")) {
                memberResumeAt = at_;
                at_ = d.paramsAt;
            }

            // [class.union]/5: what an anonymous union may not hold, each refused at the name clang points at;
            // a member whose class has special members of its own is the unrestricted union, not built.
            if (anonProper) {
                if (memberIsFunction)
                    src_.fail(d.pos, "functions cannot be declared in an anonymous union");
                if (msc == StorageStatic)
                    src_.fail(d.pos, "static data member '" + d.name + "' not allowed in anonymous union");
                if (access != Access::Public)
                    src_.fail(d.pos, std::string("anonymous union cannot contain a ") +
                                     (access == Access::Private ? "private" : "protected") + " data member");
                const Type *mc = d.type->unqualified();
                while (mc->isArray()) mc = mc->pointee()->unqualified();
                if (mc->isStructOrUnion() && !mc->tag().empty() &&
                    (hasConstructors(mc->tag()) || destructorOf(mc) != nullptr ||
                     copyConstructorOf(mc) != nullptr || moveConstructorOf(mc) != nullptr ||
                     copyAssignOf(mc) != nullptr || moveAssignOf(mc) != nullptr))
                    src_.fail(d.pos, "'" + d.name + "' is a member of an anonymous union, and its class "
                                     "'" + mc->tag() + "' has a constructor, destructor or assignment of "
                                     "its own - a union holding one is not supported yet: [class.union]/2 "
                                     "would delete the enclosing class's own special members");
                anonUnionMemberPos_[type].push_back(d.pos);
            }

            // **[dcl.stc]/9 names what `mutable` may not be applied to**, and
            // each of the four would be a contradiction rather than a gap.
            if (isMutable) {
                if (msc == StorageStatic || memberIsFunction ||
                    d.type->isReference() || d.type->isConst())
                    src_.fail(d.pos, "'mutable' may not be applied to '" +
                                     d.name + "' - [dcl.stc] allows it on a "
                                     "non-static data member that is neither "
                                     "const nor a reference");
            }

            // **`constexpr` on a member function, taken off the return type here as
            // well.** The out-of-line path does the same, and a member declared here
            // is defined through that path - so without this the two disagree.
            if (mquals.isConstexpr && memberIsFunction &&
                !d.type->isFunction())
                d.type = types_.withoutConst(d.type);

            // [class.union]/2: a union holds no reference, clang's words at the name.
            if (kind == Kind::Union && !memberIsFunction && d.type->isReference())
                src_.fail(d.pos, "union member '" + d.name + "' has reference type '" + d.type->describe() + "'");
            // **A reference member is bound, never assigned**, so a class with one
            // and no constructor could never be built. Said here rather than at the
            // first use, where the reader hears only that something is uninitialised.
            if (!memberIsFunction && d.type->isReference() &&
                !hasConstructors(tag) && !peek().is(";"))
                src_.fail(d.pos, "'" + d.name + "' is a reference member, and a "
                                 "reference is bound where it is made - so this "
                                 "class needs a constructor with '" + d.name +
                                 "' in its initialiser list");

            // **A static member is not part of the object**, so it leaves the
            // layout untouched and the cursor where it was.
            if (msc == StorageStatic && !peek().is("(")) {
                // [class.local]/4: a local class - or one nested in it - has no static data member.
                for (const Type *c = type; c != nullptr; c = c->enclosing())
                    if (!c->unqualified()->localOwner().empty())
                        src_.fail(d.pos, "'" + d.name + "' is a static data member of the local class '" +
                                         local + "', and a local class cannot have one - [class.local]/4");
                d.type = constexprObjectType(d.type, mquals);
                declareStaticMember(tag, type, d, access, mquals.isThreadLocal,
                                    mquals.isConstexpr);
                if (!consume(",")) break;
                continue;
            }
            // [dcl.stc]/4: a static data member may be thread_local; nothing else in a class may.
            refuseThreadLocalHere(mquals.isThreadLocal || mquals.threadLocalAsStatic, d.pos);

            if (peek().is(":")) {
                std::size_t cpos = peek().pos;
                at_++;
                long long w = constantExpression("a bit-field width");
                if (!d.type->isInteger())
                    src_.fail(d.pos, "a bit-field must have an integer type, not '" +
                                     d.type->describe() + "'");
                if (mquals.alignAs != 0)
                    src_.fail(d.pos, "'alignas' cannot be applied to the bit-field '" + d.name + "' - "
                                     "[dcl.align]/1");
                long long unitBits = d.type->size(target_) * 8;
                if (w < 0)
                    src_.fail(cpos, "'" + d.name + "' has a bit-field width of " +
                                    std::to_string(w) + ", which cannot be negative");
                if (w == 0)
                    src_.fail(cpos, "'" + d.name + "' has a bit-field width of 0; "
                                    "only an unnamed bit-field may be zero, and it "
                                    "means 'start the next storage unit'");
                if (w > unitBits)
                    src_.fail(cpos, "'" + d.name + "' is " + std::to_string(w) +
                                    " bits, which does not fit in '" +
                                    d.type->describe() + "'");

                int a = Packed::to(d.type->align(target_), pack);
                if (a > widest) widest = a;

                long long at, bitOff;
                if (kind == Kind::Union) {
                    at = 0;
                    bitOff = 0;
                    if (w > widestBits) widestBits = w;
                } else if (msBits) {
                    // A new unit whenever the declared type is not the one that
                    // opened the current unit, or what is open cannot hold this
                    // field. **And a unit occupies its whole width once opened.**
                    if (msUnitBits != unitBits ||
                        bitCursor - msUnitStart + w > msUnitBits) {
                        const long long full = msUnitBits != 0
                            ? msUnitStart + msUnitBits : bitCursor;
                        const long long byteCursor =
                            ((bitCursor > full ? bitCursor : full) + 7) / 8;
                        msUnitStart = alignTo(byteCursor, a) * 8;
                        msUnitBits = unitBits;
                        bitCursor = msUnitStart;
                    }
                    at = msUnitStart / 8;
                    bitOff = bitCursor - msUnitStart;
                    bitCursor += w;
                } else {
                    if (bitCursor % unitBits + w > unitBits)
                        bitCursor = alignTo(bitCursor, unitBits);
                    at = (bitCursor / unitBits) * d.type->size(target_);
                    bitOff = bitCursor % unitBits;
                    bitCursor += w;
                }
                refuseDuplicateMember(members, ownFrom, d.name, tag, d.pos);
                members.push_back(Member{ d.name, d.type, static_cast<int>(at),
                                          static_cast<int>(w),
                                          static_cast<int>(bitOff), access,
                                          isMutable });
                if (!consume(",")) break;
                continue;
            }

            // A '(' after the name is a member function and not a member.
            if (peek().is("(")) {
                // **[class.ctor]/3: a constructor has no return type**, so a
                // member with the class's own name and one written in front of
                // it is not a declaration this compiler can make sense of.
                if (!tag.empty() && d.name == local)
                    src_.fail(d.pos, "a constructor has no return type - "
                                     "'" + local + "' names the class, so this "
                                     "declares one, and [class.ctor] gives it "
                                     "no type to return. Drop the '" +
                                     d.type->describe() + "' - `void` is a "
                                     "type like any other here, and clang "
                                     "refuses that spelling too");
                // [class.free]/1 and /2: allocation and deallocation
                // functions are static members whether or not they say so.
                const bool memberIsStatic = msc == StorageStatic ||
                    d.name == "operatornew" || d.name == "operatordelete" ||
                    d.name == "operatornew[]" || d.name == "operatordelete[]";
                std::vector<const Type *> mparams;
                bool mvariadic = false;
                const std::size_t paramsOpen = at_;
                const Type *writtenReturn = d.type;
                parameterTypes(mparams, mvariadic);
                if (memberResumeAt != 0) at_ = memberResumeAt;
                skipAttributes();
                d.type = types_.functionType(d.type, std::move(mparams), mvariadic);
                // `f() const`, `f() volatile`, `f() const volatile` in either order: the cv on `this`
                // is part of a member function's identity on both ABIs, [dcl.fct]/6.
                bool constThis = false, volatileThis = false;
                for (;;) {
                    if (consume("const")) { constThis = true; continue; }
                    if (consume("volatile")) { volatileThis = sawVolatile(); continue; }
                    break;
                }
                // [dcl.fct]/6: the ref-qualifier, after the cv - `f() &`, `f() const &&`.
                const std::size_t refAt = peek().pos;
                const RefQual refThis = refQualifier();
                // [class.static]/1: a static member function has no `this`, so
                // there is nothing for either of these to qualify.
                if (memberIsStatic && (constThis || volatileThis))
                    src_.fail(d.pos, "'" + d.name + "' is a static member "
                                     "function and has no 'this', so '" +
                                     (constThis ? "const" : "volatile") + "' has nothing to qualify");
                pendingVolatileThis_ = volatileThis;
                if (memberIsStatic && refThis != RefQual::None)
                    src_.fail(refAt, "static member function '" + d.name + "' cannot have a "
                                     "ref-qualifier - it has no 'this' for one to qualify");
                if (memberIsStatic && isVirtual)
                    src_.fail(d.pos, "'" + d.name + "' cannot be both 'static' "
                                     "and 'virtual' - one says there is no "
                                     "object and the other dispatches on one");
                pendingNoexcept_ = exceptionSpecification();
                if (mentionsDeduced(writtenReturn) && peek().is("->")) {
                    refuseTrailingBase(writtenReturn, d.pos);
                    d.type = types_.functionType(readTrailingReturn(paramsOpen),
                                                 d.type->params(), mvariadic);
                } else if (mentionsDeduced(writtenReturn)) {
                    src_.fail(d.pos, "a function's return type cannot be deduced - `auto` "
                                     "there is C++14, and this compiler is C++11");
                }
                // [class.virtual]/4: `override` and `final` mark a virtual member function, which a static one is never.
                if (memberIsStatic) refuseVirtSpecifiers("a static member function");
                const VirtSpecifiers virt = takeVirtSpecifiers();
                // **A `constexpr` member function is implicitly const in C++11.**
                if (mquals.isConstexpr) constThis = true;

                // **`= default` is for the copy and move assignment alone here** - [dcl.fct.def.default]/1 -
                // and is the implicit one, so nothing is declared; `= delete` declares and stamps.
                const SpecialDef special = takeSpecialDef();
                // [class.conv.fct]/1: a conversion function takes no parameter and is written with no return type.
                if (isConversionName(d.name)) {
                    if (!d.type->params().empty())
                        src_.fail(d.pos, "a conversion function takes no parameters - [class.conv.fct]/1");
                    std::size_t k = itemStart;
                    while (tokens_[k].is("virtual") || tokens_[k].is("inline") || tokens_[k].is("explicit") ||
                           tokens_[k].is("constexpr") || tokens_[k].is("friend"))
                        k++;
                    if (!tokens_[k].is("operator"))
                        src_.fail(d.pos, "a conversion function is written with no return type - its name says "
                                         "what it returns, [class.conv.fct]/1");
                }
                if (special == SpecialDef::Defaulted) {
                    const Type *self = tag.empty() ? nullptr : findTypedef(tag);
                    const Type *p = d.type->params().size() == 1 ? d.type->params()[0] : nullptr;
                    const bool ofSelf = p != nullptr && p->isReference() && self != nullptr &&
                                        p->referent()->unqualified() == self->unqualified();
                    if (d.name != "operator=" || !ofSelf || memberIsStatic)
                        src_.fail(pendingSpecialAt_, "'" + d.name + "' is not a special member function, "
                                                     "and only one of those may be defaulted - "
                                                     "[dcl.fct.def.default]/1");
                    Defaulted &df = defaulted_[tag];
                    if (p->isRValueReference()) defaultedMember(df.moveAssign, access, d.pos, "the move assignment");
                    else                        defaultedMember(df.copyAssign, access, d.pos, "the copy assignment");
                    if (!consume(",")) break;
                    continue;
                }

                // **`= 0` is the pure-specifier**, and not an initialiser.
                bool isPure = false;
                if (peek().is("=") && peekAt(1).kind == TokenKind::Num &&
                    !peekAt(1).isFloat && peekAt(1).value == 0) {
                    if (memberIsStatic)
                        src_.fail(peek().pos, "'" + d.name + "' cannot be both "
                                              "'static' and pure");
                    at_ += 2;
                    isPure = true;
                }

                // **The body is held, not parsed.** It has to be able to see members
                // declared after it, so nothing in it can be read until the class is
                // closed - which is why this is a delayed parse and not a recursion.
                if (peek().is("{") || peek().is("try")) {
                    if (tag.empty())
                        src_.fail(d.pos, "a member function needs a class with "
                                         "a name - this one is anonymous");
                    const std::size_t sigAt = functions_.size();
                    declareMember(tag, d, constThis, refThis, access,
                                  kind == Kind::Union, isVirtual, memberIsStatic,
                                  isPure, virt);
                    if (functions_.back().isVirtual) checkOverride(type, functions_.back(), false, d.pos);
                    pendingBodies_.push_back(PendingBody{
                        tag, itemStart, local, tag + "::" + d.name,
                        signatureAddedUnder(tag + "::" + d.name, sigAt) });
                    markInlineBody(pendingBodies_.back().which);
                    skipBracedBlock();
                    heldBody = true;
                    break;
                }
                if (tag.empty())
                    src_.fail(d.pos, "a member function needs a class with a "
                                     "name - this one is anonymous");
                declareMember(tag, d, constThis, refThis, access, kind == Kind::Union,
                              isVirtual, memberIsStatic, isPure, virt);
                if (functions_.back().isVirtual)
                    checkOverride(type, functions_.back(), special == SpecialDef::Deleted, d.pos);
                if (special == SpecialDef::Deleted) {
                    // A deleted virtual's slot holds the runtime's trap, as a pure one's does.
                    if (functions_.back().isVirtual) {
                        const std::string sym = functions_.back().symbol;
                        deletedVirtuals_.insert(sym);
                        for (VSlot &s : vtables_[tag])
                            if (s.symbol == sym) s.symbol = deletedVirtualSymbol();
                    }
                    functions_.back().deleted = true;
                }
                if (!consume(",")) break;
                continue;
            }

            if (isVirtual)
                src_.fail(d.pos, "'virtual' describes a function, and '" +
                                 d.name + "' is a data member");

            realizeMemberClass(d.type, true);
            if (!d.type->isComplete())
                src_.fail(d.pos, "'" + d.name + "' has an incomplete type");
            checkNotAbstract(d.type, d.pos, "the member '" + d.name + "'");
            // **What a reference member occupies is a pointer**, and asking the type
            // is the wrong question: `sizeof` a reference is its referent's size. The
            // declared type stays the reference, which makes every read dereference.
            const Type *slot = d.type->isReference()
                             ? types_.pointerTo(d.type->referent()) : d.type;
            int a = Packed::to(slot->align(target_), pack);
            refuseWeakAlignas(mquals.alignAs, slot, d.pos);
            if (mquals.alignAs > a) a = mquals.alignAs;
            if (a > widest) widest = a;
            const long long openEnd = (msBits && msUnitBits != 0)
                ? msUnitStart + msUnitBits : bitCursor;
            long long byteCursor =
                ((bitCursor > openEnd ? bitCursor : openEnd) + 7) / 8;
            long long at = (kind == Kind::Union) ? 0 : alignTo(byteCursor, a);
            refuseDuplicateMember(members, ownFrom, d.name, tag, d.pos);
            {
                // A class-typed member, or an array of them, hands its flag on; a
                // scalar leaves the flag as it stood. On Itanium it also steps past
                // any empty subobject of its own type, element by element.
                const Type *element = slot;
                long long count = 1;
                while (element->isArray()) { count *= element->length(); element = element->pointee(); }
                if (element->unqualified()->isStructOrUnion()) {
                    endsZero = element->unqualified()->endsWithZeroSized();
                    const int each = element->size(target_);
                    if (!target_.microsoftNames() && kind != Kind::Union) {
                        bool clash = true;
                        while (clash) {
                            clash = false;
                            for (long long i = 0; i < count && !clash; i++)
                                clash = Conflict::at(empties, element, static_cast<int>(at + i * each));
                            if (clash) at += a;
                        }
                    }
                    for (long long i = 0; i < count; i++)
                        Conflict::add(empties, element, static_cast<int>(at + i * each));
                }
            }
            members.push_back(Member{ d.name, d.type, static_cast<int>(at), 0, 0,
                                      access, isMutable });
            // `int x = 5;` - C++11's initialiser on the member itself. The tokens stay
            // where they are and their place is recorded; every constructor that does
            // not name this member in its own list reads them again.
            if (peek().is("=") || peek().is("{")) {
                if (kind == Kind::Union && unionInits++ > 0)
                    src_.fail(d.pos, "initializing multiple members of union");
                consume("=");
                // `= {}` and `= {0}` zero the member, [dcl.init.list]/3; `{v}` on a scalar is the
                // value, narrowing checked at the replay; a list for a class or an array is built
                // there as a local's would be. A reference takes one value.
                if (peek().is("{")) {
                    const bool zeroing = peekAt(1).is("}") ||
                        (peekAt(1).kind == TokenKind::Num && !peekAt(1).isFloat && peekAt(1).value == 0 &&
                         peekAt(2).is("}"));
                    if (!zeroing && d.type->isReference())
                        src_.fail(peek().pos, "a braced member initialiser on a reference is not "
                                              "supported yet - write '= v'");
                    // [dcl.init.list]/7 holds whether or not a constructor ever reads it: a literal is
                    // checked here, where nothing else in the class need be known.
                    const std::size_t lit = peekAt(1).is("-") ? 2 : 1;
                    if (!d.type->isReference() && d.type->isScalar() && peekAt(lit).kind == TokenKind::Num &&
                        peekAt(lit + 1).is("}")) {
                        const std::size_t save = at_;
                        at_++;
                        {
                            Discarded held(this);
                            ExprPtr v = assign();
                            checkNarrowing(d.type, *v, d.pos, "'" + d.name + "'");
                        }
                        at_ = save;
                    }
                }
                memberInit_[tag + "::" + d.name] = at_;
                skipMemberInitialiser();
            }
            long long endBits = (at + slot->size(target_)) * 8;
            if (kind == Kind::Union) { if (endBits > widestBits) widestBits = endBits; }
            else bitCursor = endBits;
            msUnitBits = 0;          // a member that is not a bitfield closes the unit
            if (!consume(",")) break;
        }
        if (!heldBody) expect(";");
    }
    expect("}");
    classStack_.pop_back();

    // Polymorphism is only knowable now, so the vptr slot is made here.
    const bool anyVirtual = !tag.empty() && !vtables_[tag].empty();
    // **Polymorphic stays the language's question** - a virtual function,
    // declared or inherited - because `dynamic_cast` and `typeid` hang off it.
    if (anyVirtual || (base != nullptr && base->polymorphic()))
        type->setPolymorphic(true);
    if (target_.microsoftNames())
        for (std::size_t bi = 0; bi < vbases.size(); bi++)
            if (vbases[bi]->polymorphic()) type->setPolymorphic(true);

    // **A written virtual base earns a vptr as surely as a virtual function
    // does**, and does not thereby make the class polymorphic: the vptr is
    // there to reach the base, not to dispatch.
    bool writesVirtualBase = false;
    for (std::size_t bi = 0; bi < written.size(); bi++)
        if (written[bi].isVirtual) writesVirtualBase = true;
    for (std::size_t bi = 0; bi < written.size() && !writesVirtualBase; bi++)
        if (written[bi].type->hasVirtualBase()) writesVirtualBase = true;

    // **The Microsoft ABI has two pointers where Itanium has one.**
    if (target_.microsoftNames() && kind != Kind::Union) {
        const Type *firstNv = nullptr;
        for (std::size_t bi = 0; bi < written.size(); bi++) {
            if (written[bi].isVirtual || !written[bi].type->polymorphic()) continue;
            if (firstNv == nullptr) firstNv = written[bi].type;
        }
        const bool inheritsVfptr = firstNv != nullptr && firstNv->polymorphic();
        // **The vftable pointer goes in front of everything the class holds**,
        // bases included, and no base supplies one here.
        const int added = anyVirtual && !inheritsVfptr ? 8 : 0;
        if (added != 0) {
            for (std::size_t i = 0; i < members.size(); i++)
                members[i].offset += added;
            type->shiftBaseOffsets(added);
            if (msVbptrAt >= 0) msVbptrAt += added;
            bitCursor += static_cast<long long>(added) * 8;
            if (widest < 8) widest = 8;
        }
        if (msVbptrAt >= 0) {
            type->addVbptr(msVbptrAt, type, nullptr);
        } else if (writesVirtualBase) {
            // **Inherited, and there can be more than one.**
            const std::vector<Type::BaseSpec> &laid = type->bases();
            for (std::size_t bi = 0; bi < laid.size(); bi++) {
                if (laid[bi].isVirtual) continue;
                if (laid[bi].type->vbptrOffset() < 0) continue;
                type->addVbptr(laid[bi].offset +
                                   laid[bi].type->vbptrOffset(),
                               laid[bi].type->vbptrOwner(), laid[bi].type);
            }
        }
    } else if ((anyVirtual || writesVirtualBase) && !inheritsVptr &&
               kind != Kind::Union) {
        // **The same rule on Itanium, and the same correction.** With no
        // primary base to take a vptr from, one goes at offset 0, in front of
        // every base (clang: `Z : A` is vptr 0, A 8, z 12).

        // The vptr is one pointer wide - four on the C6000 - and what follows
        // keeps its own alignment: a double after a four-byte vptr sits at 8,
        // so the shift is the vptr rounded up to the widest alignment so far.
        const int vptr = pointerBytes();
        const int slot = alignTo(vptr, widest);
        for (std::size_t i = 0; i < members.size(); i++)
            members[i].offset += slot;
        type->shiftBaseOffsets(slot);
        bitCursor += static_cast<long long>(slot) * 8;
        if (widest < vptr) widest = vptr;
    }

    // The open unit's full width counts toward the class's size, not just the
    // bits a field used in it.
    const long long lastBits = (msBits && msUnitBits != 0 &&
                                msUnitStart + msUnitBits > bitCursor)
                             ? msUnitStart + msUnitBits : bitCursor;
    long long totalBits = (kind == Kind::Union) ? widestBits : lastBits;
    // Everything above is the non-virtual part; the virtual bases follow it.
    // **Recorded only when there are any.**
    bool anyVirtualBase = false;
    for (std::size_t bi = 0; bi < written.size(); bi++)
        if (written[bi].isVirtual) anyVirtualBase = true;
    for (std::size_t bi = 0; bi < written.size() && !anyVirtualBase; bi++)
        if (written[bi].type->hasVirtualBase()) anyVirtualBase = true;
    // **cl rounds the non-virtual part up to the class's alignment before it
    // appends the virtual bases**, where Itanium packs them into the padding.
    if (target_.microsoftNames() && anyVirtualBase && widest > 0)
        totalBits = alignTo(static_cast<int>((totalBits + 7) / 8), widest) * 8LL;

    if (anyVirtualBase)
        type->setNvDataSize(static_cast<int>((totalBits + 7) / 8));

    // **cl's vtordisp (/vd1)**: four bytes in front of a virtual base a base of this class had one for,
    // or one whose function this class overrides while declaring a constructor or a destructor (M3).
    std::set<const Type *> vtordisps;
    if (target_.microsoftNames()) {
        for (const Type::BaseSpec &b : type->bases()) {
            if (!b.direct) continue;
            std::map<std::string, std::set<const Type *> >::const_iterator theirs = msVtordisp_.find(b.type->tag());
            if (theirs != msVtordisp_.end()) vtordisps.insert(theirs->second.begin(), theirs->second.end());
        }
        const Signature *dt = nullptr;
        if (const std::vector<std::size_t> *set = overloadsOf(destructorKey(tag))) dt = &functions_[(*set)[0]];
        if (hasConstructors(tag) || (dt != nullptr && !dt->implicit))
            for (std::map<std::string, MsVbaseAdjust>::const_iterator it = msVbaseAdjust_.begin();
                 it != msVbaseAdjust_.end(); ++it) {
                const bool special = it->first.compare(0, 3, "??1") == 0 || it->first.compare(0, 4, "??_G") == 0;
                if (it->second.cls == tag && !special && msPure_.count(it->first) == 0)
                    vtordisps.insert(it->second.vbase);
            }
        if (!vtordisps.empty()) msVtordisp_[tag] = vtordisps;
    }
    for (std::size_t bi = 0; bi < vbases.size(); bi++) {
        const Type *b = vbases[bi];
        long long byteCursor = (totalBits + 7) / 8;
        if (vtordisps.count(b) != 0) byteCursor += 4;
        const int balign = Packed::to(b->align(target_), pack);
        const int at = static_cast<int>(alignTo(static_cast<int>(byteCursor), balign));
        const std::vector<Member> &inherited = b->members();
        for (std::size_t i = 0; i < inherited.size(); i++) {
            Member m = inherited[i];
            m.offset += at;
            // Its own virtual bases are this class's to lay down, once each:
            // the members it holds past its non-virtual part are theirs.
            if (b->hasVirtualBase() && m.offset >= b->nvDataSize() + at) continue;
            if (m.declaredIn == nullptr) m.declaredIn = b;
            if (m.access == Access::Private) m.access = Access::Private;
            else if (vaccess[bi] == Access::Private) m.access = Access::Private;
            else if (vaccess[bi] == Access::Protected) m.access = Access::Protected;
            if (m.inVirtualBase == nullptr) m.inVirtualBase = b;
            members.push_back(m);
        }
        type->setBaseOffset(nonVirtualBases + bi, at);
        totalBits = static_cast<long long>(at + b->nvDataSize()) * 8;
        if (balign > widest) widest = balign;
    }
    // **An override of a virtual base's function is entered at that base**, laid only now (M3).
    if (target_.microsoftNames())
        for (std::map<std::string, MsVbaseAdjust>::const_iterator it = msVbaseAdjust_.begin();
             it != msVbaseAdjust_.end(); ++it) {
            if (it->second.cls != tag) continue;
            for (const Type::BaseSpec &b : type->bases())
                if (b.isVirtual && b.type->unqualified() == it->second.vbase)
                    msThisAdjust_[it->first] = b.offset + it->second.within;
        }

    // **Every vbtable pointer the complete object holds** (cl's most-derived
    // constructor stores them): the virtual bases', and each non-virtual
    // base's after its first, which is shared with the base and its table.
    if (target_.microsoftNames() && kind != Kind::Union && anyVirtualBase && !type->vbptrs().empty()) {
        const std::vector<Type::BaseSpec> laid = type->bases();
        for (std::size_t bi = 0; bi < laid.size(); bi++) {
            const Type::BaseSpec &b = laid[bi];
            if (!b.isVirtual && msVbptrAt >= 0) continue;
            const std::vector<Type::VbPtr> theirs = b.type->vbptrs();
            for (std::size_t q = b.isVirtual ? 0 : 1; q < theirs.size(); q++) {
                // Only its non-virtual part's: its own virtual bases are this class's, laid down
                // elsewhere here, and in this list once each as such.
                if (b.type->hasVirtualBase() && theirs[q].offset >= b.type->nvDataSize()) continue;
                const Type *holder = q == 0 || theirs[q].base == nullptr ? b.type : theirs[q].base;
                type->addVbptr(b.offset + theirs[q].offset, theirs[q].owner, holder, b.type);
            }
        }
    }

    // **An empty class is legal in C++ and has size 1**, so that two objects of it
    // have different addresses - and it changes the numbers only: returning here once
    // dropped its held bodies. **A class whose members sum past 2^31 is refused.**
    const long long paddedBytes =
        ((totalBits + 7) / 8 + widest - 1) / widest *
        static_cast<long long>(widest);
    if (paddedBytes > 2147483647LL)
        src_.fail(pos, std::string("this ") + what + " is larger than this "
                       "compiler can lay out: its members need " +
                       std::to_string((totalBits + 7) / 8) + " bytes, past "
                       "the 2147483647 an object here is measured in");
    // [dcl.align]/5: what the class's own specifiers ask for is no weaker than what its members need.
    if (classAlign != 0 && classAlign < widest)
        src_.fail(pos, "'alignas(" + std::to_string(classAlign) + ")' is weaker than the " +
                       std::to_string(widest) + " this " + what + "'s members need - [dcl.align]/5");
    if (classAlign > widest) widest = classAlign;
    int size = static_cast<int>(alignTo((totalBits + 7) / 8, widest));
    if (sizeFloor > size) size = static_cast<int>(alignTo(static_cast<int>(sizeFloor), widest));
    int align = widest;
    if (members.empty() && totalBits == 0 && sizeFloor == 0) { size = 1; align = 1; }
    if (classAlign > align) { align = classAlign; size = static_cast<int>(alignTo(size, align)); }
    // **An empty class has sizeof 1 and a data size of 0**, which is the empty base
    // optimisation Itanium requires and both oracles do. **And tail padding is reused
    // only where the ABI says**: Itanium for a non-POD base, never on Microsoft.
    const long long unpadded = static_cast<long long>((totalBits + 7) / 8);
    const bool noData = members.empty() && totalBits == 0;
    const bool mayReuse = !noData && !target_.microsoftNames() &&
                          !podForLayout(type);
    type->setDataSize(noData ? 0
                      : mayReuse ? static_cast<int>(unpadded) : size);
    // A zero-sized class both ends and leads with one, whatever its bases said.
    type->setZeroSized(noData || endsZero, noData || leadsZero);
    if (noData) empties.push_back(Type::EmptyAt{ type, 0 });
    type->setEmptySubobjects(empties);
    type->complete(members, size, align);
    // `int (C::*f())();` written inside C: its type was made before C's model was known (1.79).
    types_.settleMemberFunctionPointers(type, target_);
    // and a member's `noexcept(e)` that named a member below it is read now (CWG 1330, 1.80)
    settleDeferredSpecs(type);
    // Held bodies are read now, the class complete, so one may name a member
    // below it; nested classes' wait for the outermost ([temp.inst]/2,
    // [class.mem]/2), a member template's specialization keeps its own.
    bool nestedInOpen = false;
    for (std::size_t i = 0; i < classStack_.size() && !nestedInOpen && specializationOf.empty(); i++)
        for (const Type *e = type->enclosing(); e != nullptr; e = e->enclosing())
            if (e->unqualified() == classStack_[i]->unqualified() && classStack_[i] != type &&
                !classStack_[i]->isComplete())
                nestedInOpen = true;
    const bool takesNested = !nestedInOpen;
    auto ownBody = [&](const PendingBody &b) {
        if (nestedInOpen) return false;
        return b.tag == tag || (takesNested && b.tag.compare(0, tag.size() + 2, tag + "::") == 0);
    };
    std::vector<PendingBody> mine;
    for (std::size_t i = 0; i < pendingBodies_.size(); i++)
        if (ownBody(pendingBodies_[i])) mine.push_back(pendingBodies_[i]);
    if (!mine.empty()) {
        std::vector<PendingBody> rest;
        for (std::size_t i = 0; i < pendingBodies_.size(); i++)
            if (!ownBody(pendingBodies_[i])) rest.push_back(pendingBodies_[i]);
        pendingBodies_.swap(rest);
    }

    declareImplicitSpecials(tag, type, pos);
    // After them: an inherited constructor is not user-declared, so it leaves the implicit default one.
    declareInheritingCtors(tag, type, pos);
    // **Who takes cl's hidden flag**, recorded once the class is laid out and
    // its implicit members declared: every constructor of a Microsoft class
    // with a virtual base, by symbol, because the flag is not in the name.
    if (target_.microsoftNames() && type->hasVirtualBase()) {
        msVbaseClasses_.insert(tag);
        if (const std::vector<std::size_t> *set =
                overloadsOf(constructorKey(tag)))
            for (std::size_t i = 0; i < set->size(); i++)
                msVbaseCtors_.insert(functions_[(*set)[i]].symbol);
    }
    // **Whether copying this is a call decides how it is passed**, and it is settled
    // here because both halves are: a copy constructor exists by now if the class
    // wrote one, or if a base or member made the copy non-trivial.
    if (copyConstructorOf(type) != nullptr || moveConstructorOf(type) != nullptr)
        type->setNonTrivialCopy(true);
    if (destructorOf(type) != nullptr) type->setHasDestructor(true);
    // **Abstract is a question about the finished table**, not about what this
    // class declared: a derived class that overrides every pure virtual has
    // replaced those entries and is concrete, and one that leaves any is not.
    if (!tag.empty()) {
        const std::vector<VSlot> &slots = vtables_[tag];
        for (std::size_t i = 0; i < slots.size(); i++)
            if (slots[i].pure) { type->setAbstract(true); break; }
        // On Microsoft a later base's pure virtual stays in that base's table, not this class's.
        if (target_.microsoftNames() && type->polymorphic())
            for (const MsVftable &t : msVftables(type))
                for (const VSlot &s : t.slots)
                    if (s.pure) type->setAbstract(true);
        // On Itanium too, a later base's table is not merged into this class's: its pure virtual stays unless
        // this class or another base overrides it. A destructor always has an overrider.
        const auto overrides = [](const VSlot &a, const VSlot &pure) {
            return !a.pure && a.name == pure.name && a.params == pure.params && a.constThis == pure.constThis &&
                   a.volatileThis == pure.volatileThis && a.refThis == pure.refThis;
        };
        for (const Type::BaseSpec &one : type->bases()) {
            if (target_.microsoftNames() || !one.direct) continue;
            for (const VSlot &s : vtables_[one.type->unqualified()->tag()]) {
                if (!s.pure || s.name == "~") continue;
                bool taken = false;
                for (const VSlot &mine : vtables_[tag]) taken = taken || overrides(mine, s);
                if (const std::vector<std::size_t> *own = overloadsOf(tag + "::" + s.name))
                    for (std::size_t k : *own) {
                        const Signature &f = functions_[k];
                        taken = taken || (f.owner == tag && f.params == s.params && f.constThis == s.constThis &&
                                          f.volatileThis == s.volatileThis && f.refThis == s.refThis);
                    }
                for (const Type::BaseSpec &other : type->bases())
                    if (other.direct && other.type != one.type)
                        for (const VSlot &theirs : vtables_[other.type->unqualified()->tag()])
                            taken = taken || overrides(theirs, s);
                if (!taken) type->setAbstract(true);
            }
        }
    }
    // A class with a virtual base needs a table though it has no virtual
    // function: the table is where the offset to that base is kept.
    if (target_.microsoftNames()) {
        if (type->polymorphic()) emitVtable(type, tag, pos);
        emitVbtable(type, tag, pos);
    } else if (type->hasVptr()) {
        emitVtable(type, tag, pos);
    }
    declareUsingMembers(tag);
    checkMemberDefaults(type);
    // **A specialization's member bodies are not replayed here.** This is in the
    // middle of whatever asked for the class, and a replay goes through topLevel,
    // which clears the locals; they are handed to the pass that defines them.
    bool insideSpecialization = false;
    for (std::size_t i = 0; i < classStack_.size(); i++)
        if (classStack_[i] != type && classStack_[i]->isSpecialization()) insideSpecialization = true;
    if (!specializationOf.empty() && deferSpecializationBodies_)
        heldForSpecialization_ = std::move(mine);
    else if (deferSpecializationBodies_ && insideSpecialization)
        // Nested in a specialization being made: compiled where called, with the specialization's own.
        pendingBodies_.insert(pendingBodies_.end(), mine.begin(), mine.end());
    else
        replayInlineBodies(std::move(mine));
    // `struct { int a; } anon_s;` - no tag and none from a typedef yet: named after its first declarator.
    if (tag.empty() && !anonProper && currentFunction_.empty()) {
        std::size_t k = 0;
        while (peekAt(k).is("*") || peekAt(k).is("&") || peekAt(k).is("&&") || peekAt(k).is("const") ||
               peekAt(k).is("volatile") || peekAt(k).is("("))
            k++;
        if (peekAt(k).kind == TokenKind::Ident)
            type->nameUnnamed((within != nullptr ? within->tag() + "::" : namespacePrefix()) + "<unnamed-type-" +
                                  peekAt(k).text + ">",
                              unnamedTypes_++);
    }
    return type;
}

bool Parser::typedefDefinesClass(std::size_t from, std::size_t to) const {
    for (std::size_t i = from; i < to && i < tokens_.size(); i++) {
        if (!tokens_[i].is("struct") && !tokens_[i].is("class") && !tokens_[i].is("union")) continue;
        std::size_t k = i + 1;
        // Attributes and `alignas(...)` may stand between the class-key and the body.
        for (;;) {
            if (k + 1 < to && tokens_[k].is("[") && tokens_[k + 1].is("[")) {
                int depth = 0;
                for (; k < to; k++) {
                    if (tokens_[k].is("[")) depth++;
                    else if (tokens_[k].is("]") && --depth == 0) { k++; break; }
                }
                continue;
            }
            if (k < to && tokens_[k].is("alignas")) {
                int depth = 0;
                for (k++; k < to; k++) {
                    if (tokens_[k].is("(")) depth++;
                    else if (tokens_[k].is(")") && --depth == 0) { k++; break; }
                }
                continue;
            }
            break;
        }
        if (k < to && (tokens_[k].is("{") || tokens_[k].is(":"))) return true;
    }
    return false;
}

// [dcl.align]/5: an alignment weaker than the type's own is ill-formed, not
// ignored - clang refuses it, and a program that asks is mistaken.
void Parser::refuseWeakAlignas(int asked, const Type *t, std::size_t pos) {
    if (asked == 0 || asked >= t->align(target_)) return;
    src_.fail(pos, "'alignas(" + std::to_string(asked) + ")' is weaker than "
                   "the " + std::to_string(t->align(target_)) + " that '" +
                   t->describe() + "' already needs");
}

// `alignas(N)` or `alignas(T)`: the alignment asked for, a power of two.
// [dcl.align]/2 - one weaker than the type's own is ignored, not an error.
int Parser::alignasSpecifier() {
    std::size_t pos = peek().pos;
    expect("alignas");
    expect("(");
    long long asked;
    // [dcl.align]/4: `alignas(Ts...)` is one alignas per member, the strictest of them taking effect.
    const auto pk = peek().kind == TokenKind::Ident && peekAt(1).is("...") && peekAt(2).is(")")
                        ? packs_.find(peek().text) : packs_.end();
    if (pk != packs_.end()) {
        at_ += 2;
        asked = 1;
        if (!pk->second.standsForItself())
            for (std::size_t k = 0; k < pk->second.types.size(); k++) {
                const long long one = pk->second.isValue ? pk->second.values[k] : pk->second.types[k]->align(target_);
                if (one < 0 || one > 4096 || (one & (one - 1)) != 0)
                    src_.fail(pos, "an alignment must be a power of two up to 4096, and " + std::to_string(one) +
                                   " is not");
                if (one > asked) asked = one;
            }
    } else if ([this] { std::size_t save = at_; bool t = atTypeName(); at_ = save; return t; }()) {
        StorageClass sc;
        const Type *t = declarator(specifiers(&sc), true).type;
        asked = t->align(target_);
    } else {
        asked = constantExpression("an alignment");
    }
    expect(")");
    if (asked < 0 || asked > 4096 || (asked & (asked - 1)) != 0)
        src_.fail(pos, "an alignment must be a power of two up to 4096, and " +
                       std::to_string(asked) + " is not");
    return static_cast<int>(asked);
}

const Type *Parser::enumSpecifier() {
    std::size_t pos = peek().pos;

    // **`enum class` is a type of its own** - [dcl.enum]/2: its enumerators stay inside it and
    // it converts to nothing without a cast (refuseScopedConversion, refuseScopedOperand).
    const bool scoped = peek().is("class") || peek().is("struct");
    if (scoped) at_++;
    skipTypeAttributes("an enumeration");

    std::string tag;
    if (peek().kind == TokenKind::Ident) { tag = peek().text; at_++; }
    if (scoped && tag.empty())
        src_.fail(pos, "a scoped enumeration needs a name - its enumerators are reached through "
                       "it, [dcl.enum]/2");

    // **An enum-base fixes the underlying type** - `enum E : unsigned char` -
    // and with it the enumeration's size, signedness and the type of each
    // enumerator; [dcl.enum]/2 wants an integral type, and bool is one (1.80: `enum E : bool` was refused).
    const Type *underlying = nullptr;
    if (consume(":")) {
        std::size_t upos = peek().pos;
        StorageClass usc;
        underlying = specifiers(&usc)->unqualified();
        if (!underlying->isInteger() || underlying->isEnumeration())
            src_.fail(upos, "an enum-base must be an integral type, and '" + underlying->describe() + "' is not");
    }
    // [dcl.enum]/5: a scoped enumeration without one is fixed at int.
    if (scoped && underlying == nullptr) underlying = types_.intType();

    // **An enum is named through what encloses it**, the same way a class is:
    // `C::Kind` inside a class and `n::Kind` inside a namespace.
    const Type *within = classStack_.empty() ? nullptr : classStack_.back();
    std::string prefix;
    if (within != nullptr) prefix = within->tag() + "::";
    else if (!namespaceStack_.empty()) prefix = namespacePrefix();

    // The tag names a type, as a class tag does: its integer underneath, but an integer
    // reaches it only by a cast (checkAssignable, rankArgument), and it promotes back.
    Type *self = nullptr;
    if (!tag.empty()) {
        const Type *before = findTypedef(prefix + tag);
        self = types_.enumType(prefix + tag,
                               underlying != nullptr ? underlying->kind() : Kind::Int);
        // [dcl.enum]/3: a redeclaration says the same thing about scope and base as the first.
        if (before != nullptr && before->unqualified() == self &&
            (self->isScopedEnumeration() != scoped ||
             (underlying != nullptr && self->kind() != underlying->kind())))
            src_.fail(pos, "'" + tag + "' was declared before as a different enumeration - a "
                           "redeclaration repeats 'class' and the enum-base exactly");
        if (scoped) self->setScoped();
        else if (underlying != nullptr) self->setFixedUnderlying();
        // -g: shown under the class's whole name, `geo::Box<int>::Kind` - the tag has the class's tag alone (1.29).
        if (within != nullptr) self->setDisplayTag(within->displayTag() + "::" + tag);
        // and mangled through the class, a specialization's `?$Box@H@` included (1.29)
        if (within != nullptr) self->setEnumScope(within);
        declareTypeName(prefix + tag, self);
    }
    // [dcl.enum]/5: an enumerator has its enumeration's type once the enum is complete, so
    // `colour c = RED` is a copy and `colour c = 1` is not. An unnamed one keeps its integer.
    const Type *valueType = self != nullptr ? self : underlying;
    const Type *narrowAs = underlying != nullptr ? underlying : types_.intType();

    // `enum class E;` and `enum E : int;` - an opaque declaration, [dcl.enum]/3; an unscoped one
    // without a base cannot be one, its size waiting on its enumerators.
    if (!peek().is("{")) {
        if (scoped || underlying != nullptr) {
            if (!peek().is(";") && !peek().is(","))
                src_.fail(peek().pos, "an opaque enumeration declaration ends at its name or "
                                      "enum-base - '" + tag + "' is declared, not used, here");
        } else if (peek().is(";") && !tag.empty()) {
            src_.fail(pos, "'enum " + tag + ";' declares an unscoped enumeration with no enum-base, which "
                           "cannot be opaque - [dcl.enum]/3; write 'enum " + tag + " : int;'");
        }
        return valueType != nullptr ? valueType : types_.intType();
    }
    if (self != nullptr && self->enumDefined())
        src_.fail(pos, "'" + tag + "' is defined twice");
    if (self != nullptr) self->setEnumDefined();
    at_++;

    // A scoped enumerator is named `E::a`, and inside the braces by its own name with the
    // underlying type ([dcl.enum]/5) - that unqualified key is taken back at the '}'.
    const std::string within_ = scoped ? prefix + tag + "::" : prefix;
    std::vector<std::pair<std::string, std::size_t> > inBody;
    std::vector<std::size_t> made;
    long long next = 0;
    const std::size_t first = enums_.size();
    while (!peek().is("}")) {
        std::size_t npos = peek().pos;
        std::string name = expectIdent("an enumerator");
        // Declared twice under this very key - not one an inline namespace's enumerator put here.
        const std::unordered_map<std::string, std::size_t>::const_iterator twice =
            enumIndex_.find(within_ + name);
        if ((twice != enumIndex_.end() && enums_[twice->second].name == within_ + name) ||
            (!scoped && twice == enumIndex_.end() && findEnum(prefix + name)))
            src_.fail(npos, "'" + name + "' is declared twice");
        if (consume("=")) {
            const long long given = constantExpression("a constant");
            next = narrowTo(given, narrowAs);
            // [dcl.enum]/5: a value the base cannot hold is ill-formed, not wrapped.
            if (underlying != nullptr && next != given)
                src_.fail(npos, "'" + name + " = " + std::to_string(given) +
                                "' does not fit the enum-base '" +
                                underlying->describe() + "'");
        }
        enumIndex_[within_ + name] = enums_.size();
        enums_.push_back(EnumConst{ within_ + name, next,
                                    scoped ? underlying : valueType });
        made.push_back(enums_.size() - 1);
        // C++11's `E::a` for an unscoped E too, [dcl.enum]/11.
        if (!scoped && !tag.empty()) enumIndex_[prefix + tag + "::" + name] = enums_.size() - 1;
        // And under the enclosing namespace's name where this one is inline.
        {
            const std::vector<std::string> alts = within == nullptr ? inlineAliasPrefixes(prefix)
                                                                    : std::vector<std::string>();
            const std::string tail = scoped ? tag + "::" + name : name;
            for (std::size_t a = 0; a < alts.size(); a++) {
                enumIndex_.insert(std::make_pair(alts[a] + tail, enums_.size() - 1));
                if (!scoped && !tag.empty())
                    enumIndex_.insert(std::make_pair(alts[a] + tag + "::" + name, enums_.size() - 1));
            }
        }
        if (scoped) {
            const std::size_t had = enumIndex_.count(prefix + name);
            inBody.push_back(std::make_pair(prefix + name,
                                            had ? enumIndex_[prefix + name] : enums_.size()));
            enumIndex_[prefix + name] = enums_.size() - 1;
        }
        if (self != nullptr) self->addEnumerator(name, next);
        next = next + 1;
        if (!consume(",")) break;
    }
    expect("}");
    for (std::size_t i = inBody.size(); i-- > 0; ) {
        if (inBody[i].second >= first) enumIndex_.erase(inBody[i].first);
        else                           enumIndex_[inBody[i].first] = inBody[i].second;
    }
    for (std::size_t i = 0; i < made.size(); i++) enums_[made[i]].type = valueType;
    if (enums_.empty()) src_.fail(pos, "enum has no enumerators");
    return valueType != nullptr ? valueType : types_.intType();
}

// The longest run of `A::B::` that names an enumeration, then the enumerator: null where there is none.
ExprPtr Parser::enumeratorThroughEnum() {
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("::")) return nullptr;
    std::string q = peek().text;
    const Type *found = nullptr;
    std::size_t took = 0;
    for (std::size_t k = 1; peekAt(k).is("::") && peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
        const Type *t = findTypedef(q);
        if (t != nullptr && t->unqualified()->isEnumeration()) { found = t->unqualified(); took = k; }
        q += "::" + peekAt(k + 1).text;
    }
    if (found == nullptr) return nullptr;
    const std::string name = peekAt(took + 1).text;
    const auto it = enumIndex_.find(found->enumTag() + "::" + name);
    if (it == enumIndex_.end())
        src_.fail(peekAt(took + 1).pos, "'" + name + "' is not an enumerator of '" +
                                        found->enumTag() + "'");
    at_ += took + 2;
    ExprPtr n(new Num(enums_[it->second].value));
    n->setType(found);
    return n;
}

// [dcl.attr.grammar]: any number of `[[...]]` - C++11's two are read and change nothing, `deprecated`
// is C++14 and refused, and every other attribute-token is ignored, /5. True if one was read.
bool Parser::skipAttributes() {
    bool any = false;
    while (peek().is("[") && peekAt(1).is("[")) {
        any = true;
        at_ += 2;
        for (;;) {
            if (peek().is("]") && peekAt(1).is("]")) { at_ += 2; break; }
            if (consume(",")) continue;
            if (peek().kind != TokenKind::Ident && peek().kind != TokenKind::Keyword)
                src_.fail(peek().pos, "expected the name of an attribute, or ']]' to close the list");
            const std::size_t apos = peek().pos;
            const std::string name = peek().text;
            at_++;
            bool scoped = false;
            if (peek().is("::") && (peekAt(1).kind == TokenKind::Ident || peekAt(1).kind == TokenKind::Keyword)) {
                at_ += 2;
                scoped = true;
            }
            const bool args = peek().is("(");
            if (args && !scoped && (name == "noreturn" || name == "carries_dependency"))
                src_.fail(peek().pos, "the attribute '" + name + "' takes no argument list");
            if (!scoped && name == "noreturn") noreturnAt_ = apos;
            if (args) skipBalanced();
            if (!scoped && name == "deprecated")
                src_.fail(apos, "the attribute '[[deprecated]]' is C++14, and this compiler is C++11 - "
                                "C++11 has '[[noreturn]]' and '[[carries_dependency]]', which are read, "
                                "and an attribute it does not know is ignored");
            consume("...");
            if (!peek().is(",") && !(peek().is("]") && peekAt(1).is("]")))
                src_.fail(peek().pos, "expected ',' or ']]' after the attribute '" + name + "'");
        }
    }
    if (any) lastAttributeEnd_ = at_;
    return any;
}

// `[[x]] int y;` in a block is a declaration with an attribute, left for the declaration's reader;
// `[[x]] return 0;` is a statement with one, [stmt.stmt]/1, read past here and the statement after
// it read as if it had none - except C++11's two, which appertain to functions alone.
bool Parser::attributedDeclarationAhead() {
    if (!peek().is("[") || !peekAt(1).is("[")) return false;
    const std::size_t save = at_;
    skipAttributes();
    const bool declares = atDeclarationStart();
    if (declares) {
        at_ = save;
        return true;
    }
    for (std::size_t i = save + 2; i < at_; i++)
        if ((tokens_[i].is("noreturn") || tokens_[i].is("carries_dependency")) &&
            (tokens_[i - 1].is("[") || tokens_[i - 1].is(",")) && !tokens_[i + 1].is("::"))
            src_.fail(tokens_[i].pos, "the attribute '" + tokens_[i].text + "' cannot be applied to a "
                                      "statement - [dcl.attr.noreturn]/1 and [dcl.attr.depend]/1 give it "
                                      "to a function or a parameter");
    return false;
}

void Parser::refuseNoreturnObject(const std::string &name) {
    if (noreturnAt_ != 0)
        src_.fail(noreturnAt_, "'noreturn' attribute only applies to functions, and '" + name +
                               "' is a variable - [dcl.attr.noreturn]/1");
}

// From an opening bracket of any kind to the one that closes it, nesting counted.
void Parser::skipBalanced() {
    const std::size_t pos = peek().pos;
    int depth = 0;
    do {
        if (peek().kind == TokenKind::End) src_.fail(pos, "this bracket never closes");
        if (peek().is("(") || peek().is("[") || peek().is("{")) depth++;
        else if (peek().is(")") || peek().is("]") || peek().is("}")) depth--;
        at_++;
    } while (depth > 0);
}

// The specifiers are read without their qualifiers here, and specifiers() folds the
// cv in afterwards. It reads 'const' and 'volatile' in two places - before the type
// name and after it - and both must be collected before the type can be built.
const Type *Parser::specifiers(StorageClass *storage, Qualifiers *quals) {
    Qualifiers discard;
    if (quals == nullptr) quals = &discard;
    fnPointerSpec_ = 0;              // a new declaration: its declarator says its own
    // A constructor template replayed from its class, `template <class Y> explicit P(Y *)`: the keyword was
    // read there, and instantiateMemberTemplate gives the specialization it.
    if (memberTemplateInst_ && !inlineOwner_.empty() && peek().is("explicit")) at_++;
    // [temp.inst]/1: a typedef at namespace or block scope names a specialization without needing it complete -
    // `struct Value; typedef std::pair<std::string, Value> Member;` (1.82: "'second' has an incomplete type").
    struct TypedefSpecifiers {
        Parser *p; bool was, defer;
        ~TypedefSpecifiers() { p->typedefSpecifiers_ = was; p->typedefDefer_ = defer; }
    } typedefSpecifiers{ this, typedefSpecifiers_, typedefDefer_ };
    if (peek().is("typedef") && classStack_.empty() && classesInstantiating_ == 0)
        typedefSpecifiers_ = typedefDefer_ = true;
    const Type *t = unqualifiedSpecifiers(storage, quals);
    // **A cv-qualifier after a type's name is the same cv-qualifier** ([dcl.type]/1: the decl-specifiers come
    // in any order): `T const &`, `std::size_t const n`, `S volatile s`. unqualifiedSpecifiers reads them after a
    // built-in type in its own loop, but returns as soon as it has a class, a typedef, a template parameter or a
    // qualified name - and before 1.67 the `const` written after one was left for the declarator, which wanted a
    // name and said "expected a name".
    while (cvSpecifier(quals)) {}
    if (quals->isConst) t = types_.withConst(t);
    if (quals->isVolatile) t = types_.withVolatile(t);
    return t;
}

// **One `const` and one `volatile` among a declaration's specifiers** ([dcl.type]/2): written twice - `const int
// const`, `S volatile volatile` - is ill-formed, before or after the type's name alike; a typedef that already
// holds one is not a second (`typedef const int CI; CI const x;`), since what it holds is in its type, not in quals.
// True when it read one.
bool Parser::cvSpecifier(Qualifiers *quals) {
    if (peek().is("const")) {
        if (quals->constWritten)
            src_.fail(peek().pos, "'const' is written twice among this declaration's specifiers, [dcl.type]/2");
        at_++;
        quals->isConst = quals->constWritten = true;
        return true;
    }
    if (peek().is("volatile")) {
        if (quals->isVolatile)
            src_.fail(peek().pos, "'volatile' is written twice among this declaration's specifiers, [dcl.type]/2");
        at_++;
        quals->isVolatile = sawVolatile();
        return true;
    }
    return false;
}

// **`volatile` is in the type system, yet its file is compiled without -O**:
// the passes do not yet tell volatile accesses apart (Program::usesVolatile).
// Marks the file and keeps where the first `volatile` was; always true.
bool Parser::sawVolatile() {
    if (current_ != nullptr) {
        if (!current_->usesVolatile && at_ > 0) current_->volatileAt = src_.where(tokens_[at_ - 1].pos);
        current_->usesVolatile = true;
    }
    return true;
}

const Type *Parser::unqualifiedSpecifiers(StorageClass *storage, Qualifiers *quals) {
    std::size_t start = peek().pos;
    *storage = StorageNone;

    // **A conversion function's name *is* its return type**, which is the one place in the language where those two are the same thing.
    {
        // **A template-id stands where a class name does**, so the scan has to
        // step over an argument list.
        std::size_t k = 0;
        while (peekAt(k).kind == TokenKind::Ident) {
            std::size_t j = k + 1;
            if (peekAt(j).is("<")) {
                int depth = 0;
                for (;;) {
                    if (peekAt(j).kind == TokenKind::End) break;
                    if (depth > 0 && (peekAt(j).is("(") || peekAt(j).is("["))) { j = groupCloseAt(at_ + j) - at_ + 1; continue; }
                    if (peekAt(j).is("<")) depth++;
                    else if (peekAt(j).is(">>")) depth -= 2;
                    else if (peekAt(j).is(">")) depth--;
                    j++;
                    if (depth <= 0) break;
                }
            }
            if (!peekAt(j).is("::")) break;
            k = j + 1;
        }
        if (peekAt(k).is("operator") && peekAt(k + 1).kind != TokenKind::Punct) {
            const std::size_t resume = at_;
            at_ += k;
            operatorName();
            const Type *to = conversionTarget_;
            conversionTarget_ = nullptr;
            at_ = resume;
            if (to != nullptr) {
                *storage = StorageNone;
                return to;
            }
        }
    }

    lastAttributeEnd_ = static_cast<std::size_t>(-1);
    // [dcl.stc]/1: one storage class to a declaration, `thread_local` beside `static` or `extern` the
    // exception; [dcl.typedef]/1 puts `typedef` with none of them. `auto` is a type here, not one of them:
    // beside a storage class it is held here, so `static auto f() -> int` stays static.
    bool autoType = false;
    auto storageClass = [&](StorageClass sc) {
        const Token &word = peek();
        if ((*storage != StorageNone && *storage != StorageAuto) ||
            (quals->isThreadLocal && sc != StorageStatic && sc != StorageExtern))
            src_.fail(word.pos, "'" + word.text + "' is a second storage class in this declaration, which "
                                "has at most one - and 'thread_local' only beside 'static' or 'extern', [dcl.stc]/1");
        if (*storage == StorageAuto) autoType = true;
        at_++;
        *storage = sc;
    };
    for (;;) {
        // The same after a specifier: `constexpr operator T() const` (tp25).
        if (peek().is("operator") && peekAt(1).kind != TokenKind::Punct) {
            const std::size_t resume = at_;
            operatorName();
            const Type *to = conversionTarget_;
            conversionTarget_ = nullptr;
            at_ = resume;
            if (to != nullptr) return to;
        }
        if (peek().is("alignas")) {
            int a = alignasSpecifier();
            if (a > quals->alignAs) quals->alignAs = a;
            continue;
        }
        if (skipAttributes()) continue;
        if (peek().is("static"))  { storageClass(StorageStatic); continue; }
        if (peek().is("extern"))  { storageClass(StorageExtern); continue; }
        // [dcl.stc]/4: `thread_local`, beside `static` or `extern` or alone. The C6000 has no
        // thread storage at all - cl6x 8.2.2 knows no such word - and runs one thread, so there the
        // object is the one static copy (Qualifiers::threadLocalAsStatic).
        if (peek().is("thread_local")) {
            if (quals->isThreadLocal || quals->threadLocalAsStatic || *storage == StorageRegister ||
                *storage == StorageTypedef)
                src_.fail(peek().pos, "'thread_local' is a second storage class in this declaration, which has "
                                      "at most one - and 'thread_local' only beside 'static' or 'extern', [dcl.stc]/1");
            at_++;
            if (target_.hasThreadStorage()) quals->isThreadLocal = true;
            else                            quals->threadLocalAsStatic = true;
            continue;
        }
        if (peek().is("typedef")) { storageClass(StorageTypedef); continue; }
        if (peek().is("const")) { cvSpecifier(quals); continue; }
        // **`constexpr` on an object is `const` plus a demand.** [dcl.constexpr]
        // makes the object const and the rest of the compiler wants to know nothing
        // else, which is why almost nothing downstream mentions constexpr at all.
        if (consume("constexpr")) {
            quals->isConst = true;
            quals->isConstexpr = true;
            continue;
        }
        if (peek().is("volatile")) { cvSpecifier(quals); continue; }
        // **`inline` is a hint about linkage, not about the type.** It makes a function's
        // definition mergeable across translation units; nothing else downstream needs it.
        // `inline namespace` at file scope is topLevel's; here it is one in a class or block.
        if (peek().is("inline") && peekAt(1).is("namespace"))
            src_.fail(peek().pos, "a namespace is defined at namespace scope, and this one is "
                                  "inside a class or a function");
        if (consume("inline")) { quals->isInline = true; continue; }
        if (peek().is("register")) { storageClass(StorageRegister); continue; }
        if (consume("auto")) {
            if (*storage == StorageNone) *storage = StorageAuto;
            else autoType = true;
            continue;
        }
        break;
    }

    // `auto S::f() -> T`: after `auto` a qualified declarator-id, not a type, [dcl.fct]/2.
    if ((*storage == StorageAuto || autoType) && autoBeforeQualifiedName()) {
        if (*storage == StorageAuto) *storage = StorageNone;
        return quals->isConst ? types_.withConst(types_.deducedType()) : types_.deducedType();
    }
    // wchar_t is a type of its own in C++, not the typedef C makes it.
    if (consume("wchar_t")) return types_.get(Kind::WChar);
    if (consume("char16_t")) return types_.get(Kind::Char16);
    if (consume("char32_t")) return types_.get(Kind::Char32);
    // **`Point::Point(...)` has no type before the name, and the name is a type.**
    if (atUntypedMemberDefinition()) return types_.get(Kind::Void);

    // Replaying an inline constructor or destructor: the tokens are `X(` or
    // `~X(` with no type in front, exactly as they were written in the class.
    if (!inlineOwner_.empty() &&
        ((peek().kind == TokenKind::Ident && peek().text == inlineOwnerName_ &&
          (peekAt(1).is("(") || ownArgsThenParen())) ||
         (peek().is("~") && peekAt(1).kind == TokenKind::Ident &&
          peekAt(1).text == inlineOwnerName_)))
        return types_.get(Kind::Void);

    // **`typename` is a hint this compiler does not need, so it is read and dropped.**
    // It tells a parser that a dependent qualified name is a type, which matters only
    // where a body is parsed before its arguments; this one replays at instantiation.
    bool sawTypename = false;
    struct TypenameScope {
        bool &flag; bool was;
        ~TypenameScope() { flag = was; }
    } typenameScope{ typenameNamed_, typenameNamed_ };
    typenameNamed_ = false;
    if (consume("typename")) {
        sawTypename = true;
        typenameNamed_ = true;
        if (peek().kind != TokenKind::Ident)
            src_.fail(peek().pos, "'typename' introduces a qualified type "
                                  "name, and this is not one");
    }
    // **`X::type` with X a template parameter needs the `typename`** -
    // [temp.res]/3, cl's C7510. The body is replayed with X bound, so the
    // member is found either way; the rule is kept because cl keeps it (A27).
    if (!sawTypename && peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
        peekAt(2).kind == TokenKind::Ident &&
        boundTypeParams_.find(peek().text) != boundTypeParams_.end())
        src_.fail(peek().pos, "'" + peek().text + "::" + peekAt(2).text +
                              "' names a member of the template parameter '" +
                              peek().text + "', and whether that is a type "
                              "depends on the argument - write 'typename " +
                              peek().text + "::" + peekAt(2).text + "'");

    // A class template with its arguments *is* a type. A function template
    // named where a type was expected is not, and is refused by name.
    // [basic.lookup.unqual]/7: a member class template is found in its class before anything outside it.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("<"))
        if (const TemplateDecl *mct = memberClassTemplate(peek().text)) {
            const TemplateDecl decl = *mct;
            const std::size_t tpos = peek().pos;
            at_++;
            return memberTypeWalk(instantiateClass(decl, tpos));
        }
    if (peek().kind == TokenKind::Ident && isTemplateName(peek().text) &&
        peekAt(1).is("<")) {
        const TemplateDecl decl = findTemplate(peek().text)->second;
        if (!decl.isClass) refuseTemplateId();
        if (!templateReachable(decl, peek().text))
            src_.fail(peek().pos, "'" + peek().text + "' is a template of the namespace '" + decl.ns.substr(0, decl.ns.size() - 2) +
                                  "', and nothing brings it here - [basic.lookup.unqual]; write '" + decl.ns + peek().text +
                                  "', or a using-declaration");
        const std::size_t tpos = peek().pos;
        at_++;
        const Type *cls = instantiateClass(decl, tpos);

        return memberTypeWalk(cls);
    }

    // `decltype(d)::period` - [dcl.type.simple]/4's specifier as a nested-name-specifier, its member type.
    if (peek().is("decltype")) {
        const Type *t = decltypeSpecifier();
        if (peek().is("::") && peekAt(1).kind == TokenKind::Ident && t->unqualified()->isStructOrUnion())
            return memberTypeWalk(t->unqualified());
        return t;
    }
    if (peek().is("__underlying_type")) return underlyingTypeSpecifier();

    if (peek().is("struct")) { at_++; return structOrUnionSpecifier(Kind::Struct); }
    if (peek().is("class"))  { at_++; return structOrUnionSpecifier(Kind::Struct, true); }
    if (peek().is("union"))  { at_++; return structOrUnionSpecifier(Kind::Union); }
    if (peek().is("enum"))   { at_++; return enumSpecifier(); }
    if (peek().kind == TokenKind::Ident) {
        // **`Outer::Inner x;` - a nested class named from outside.** Asked before the
        // plain lookup, which would take only "Outer" and leave "::Inner" for the
        // declarator to read as the name being declared. The longest prefix wins.
        if (peekAt(1).is("::") && peekAt(2).kind == TokenKind::Ident) {
            std::string q = peek().text;
            const Type *found = nullptr;
            std::string foundAs;
            std::size_t consumed = 0;
            // **`std::vector<int>` - a class template named qualified.**
            std::string tmpl;
            std::size_t tmplConsumed = 0;
            // And `S::In<int>`, a member class template of the class the prefix names.
            const Type *scope = findTypedef(q);
            const TemplateDecl *member = nullptr;
            for (std::size_t k = 1; peekAt(k).is("::") &&
                                    peekAt(k + 1).kind == TokenKind::Ident;
                 k += 2) {
                const std::string piece = peekAt(k + 1).text;
                q += "::" + piece;
                const TemplateDecl *m = peekAt(k + 2).is("<") ? memberClassTemplateOf(scope, piece) : nullptr;
                if (const Type *n = findTypedef(q)) {
                    found = n;
                    foundAs = q;
                    consumed = k + 2;
                    scope = n;
                } else {
                    scope = scope != nullptr && scope->isStructOrUnion() ? lookupInClass(scope, piece) : nullptr;
                }
                if (m != nullptr || (peekAt(k + 2).is("<") && isClassTemplate(q) && templateQualifiedBy(q))) {
                    member = m;
                    tmpl = q;
                    tmplConsumed = k + 2;
                }
            }
            if (tmplConsumed > consumed) {
                const std::size_t tpos = peek().pos;
                at_ += tmplConsumed;
                const TemplateDecl decl = member != nullptr ? *member : findTemplate(tmpl)->second;
                if (member != nullptr) checkMemberTemplateAccess(decl, peekAt(tmplConsumed - 1).pos);
                const Type *cls = instantiateClass(decl, tpos);
                return memberTypeWalk(cls);
            }
            if (found != nullptr) {
                // A nested class is a member, and `private:` reaches it.
                if (found->enclosing() != nullptr &&
                    found->nestedAccess() != Access::Public &&
                    !insideClass(found->enclosing()) &&
                    !definesMemberOf(found->enclosing(), consumed))
                    src_.fail(peek().pos, "'" + found->localName() + "' is " +
                                          (found->nestedAccess() == Access::Private
                                               ? "private" : "protected") +
                                          " in '" +
                                          found->enclosing()->tag() + "'");
                if (!currentFunction_.empty())
                    checkMemberTypeAccess(findTypedef(foundAs.substr(0, foundAs.rfind("::"))),
                                          peekAt(consumed - 1).text, peekAt(consumed - 1).pos);
                at_ += consumed;
                // `std::string::size_type`: what the prefix names may be a typedef for a class.
                return memberTypeWalk(found);
            }
        }
        if (const Type *t = findTypedef(peek().text)) {
            at_++;
            return memberTypeWalk(t);
        }
    }

    // **A leading `::` names the global scope and nothing nearer**, which is what a class writes
    // when a member, a local or a namespace has taken the name it wants - `::Lexer *lexer;` inside
    // a `cc::Parser` that also knows a `Lexer`.
    if (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        const std::map<std::string, TemplateDecl>::const_iterator named = findTemplate("::" + peekAt(1).text);
        if (peekAt(2).is("<") && named != templates_.end() && named->second.isClass && named->second.ns.empty()) {
            const TemplateDecl decl = named->second;
            const std::size_t tpos = peekAt(1).pos;
            at_ += 2;
            return memberTypeWalk(instantiateClass(decl, tpos));
        }
        if (const Type *t = findGlobalTypedef(peekAt(1).text)) {
            at_ += 2;
            return memberTypeWalk(t);
        }
    }

    int isVoid = 0, isBool = 0, isChar = 0, isShort = 0, isInt = 0, isLong = 0;
    int isSigned = 0, isUnsigned = 0, isFloat = 0, isDouble = 0;

    while (atTypeName()) {
        // atTypeName() is also true for an identifier naming a typedef and nothing
        // below consumes one, so without this the loop spins forever on
        // `typedef long T;`. Inherited from Compiler-C, where it hangs too.
        if (peek().kind == TokenKind::Ident) break;
        if (peek().is("const"))       { cvSpecifier(quals); continue; }
        if (consume("constexpr")) {
            quals->isConst = true;
            quals->isConstexpr = true;
            continue;
        }
        if (peek().is("volatile"))    { cvSpecifier(quals); continue; }
        if (consume("float"))         isFloat++;
        else if (consume("double"))   isDouble++;
        else if (consume("void"))     isVoid++;
        else if (consume("bool"))     isBool++;
        else if (consume("char"))     isChar++;
        else if (consume("short"))    isShort++;
        else if (consume("int"))      isInt++;
        else if (consume("long"))     isLong++;
        else if (consume("signed"))   isSigned++;
        else if (consume("unsigned")) isUnsigned++;
    }

    if (isBool && (isVoid || isChar || isShort || isInt || isLong ||
                   isSigned || isUnsigned || isFloat || isDouble))
        src_.fail(start, "'bool' cannot be combined with another specifier");
    if (isSigned && isUnsigned)
        src_.fail(start, "'signed' and 'unsigned' together is not a type");
    if (isVoid && (isChar || isShort || isInt || isLong || isSigned || isUnsigned))
        src_.fail(start, "'void' cannot be combined with another specifier");
    if (isChar && (isShort || isInt || isLong))
        src_.fail(start, "'char' cannot be combined with that");
    if (isShort && isLong) src_.fail(start, "'short long' is not a type");
    if (isLong > 2)        src_.fail(start, "'long long' is not a type");
    if ((isFloat || isDouble) && (isChar || isShort || isInt || isSigned || isUnsigned))
        src_.fail(start, "a floating type cannot be combined with that");
    if (isFloat && isDouble)
        src_.fail(start, "'float double' is not a type");
    if (isDouble && isLong > 1)
        src_.fail(start, "'long long double' is not a type");

    if (isBool)   return types_.get(Kind::Bool);
    if (isFloat)  return types_.get(Kind::Float);
    if (isDouble) return types_.get(isLong ? Kind::LongDouble : Kind::Double);
    if (isVoid)  return types_.get(Kind::Void);
    if (isChar)  return types_.get(isUnsigned ? Kind::UChar
                                  : isSigned ? Kind::SChar : Kind::Char);
    if (isShort) return types_.get(isUnsigned ? Kind::UShort : Kind::Short);
    if (isLong == 2) return types_.get(isUnsigned ? Kind::ULongLong : Kind::LongLong);
    if (isLong)  return types_.get(isUnsigned ? Kind::ULong : Kind::Long);
    if (isInt || isSigned || isUnsigned)
        return types_.get(isUnsigned ? Kind::UInt : Kind::Int);

    // **In C++11 `auto` is a type specifier, not the storage class C90 made it.** This
    // parser still reads it as one, so reaching here having consumed it is exactly
    // where a type was to be deduced: the storage class is dropped, a stand-in given.
    if (*storage == StorageAuto || autoType) {
        if (*storage == StorageAuto) *storage = StorageNone;
        const Type *deduced = types_.deducedType();
        return quals->isConst ? types_.withConst(deduced) : deduced;
    }
    // Same reason as in expectIdent: `friend`, `mutable`, `explicit`, `using`
    // and `static_assert` all begin a member declaration in C++ and none
    // begins one here.
    if (peek().is("operator"))
        src_.fail(peek().pos, "'operator' here names neither an operator this "
                              "compiler can overload nor a type it can convert "
                              "to - a conversion function's name is a type, and "
                              "every operator that can be overloaded is "
                              "punctuation");
    if (peek().is("explicit"))
        src_.fail(peek().pos, "'explicit' is written on a constructor's "
                              "declaration inside its class, and nowhere else "
                              "- not on an out-of-class definition of that "
                              "same constructor, and not on anything that is "
                              "not one");
    // `[[x]];` is an attribute-declaration, [dcl.pre]/1, which declares nothing.
    if (lastAttributeEnd_ == at_ && peek().is(";") && *storage == StorageNone &&
        !quals->isConst && !quals->isVolatile)
        return types_.get(Kind::Void);
    if (const char *pending = notYetSupported(peek().text))
        src_.fail(peek().pos, std::string("'") + pending +
                              "' is not supported yet");
    if (const char *here = implementedElsewhere(peek().text))
        src_.fail(peek().pos, std::string("'") + here + "' is implemented, but "
                              "not where a type was wanted");
    if (*storage != StorageNone || quals->isConst || quals->isVolatile)
        src_.fail(start, "this declaration has no type; write one");
    src_.fail(start, "expected a type");
}

// **[class.mem]/1: a member may not be declared twice in one class.** cxx1 laid both out and read
// whichever `findMember` reached - which is the *last*, since that walk goes backwards so a derived
// member hides a base's - so the first one was a hole in the object nothing could name.
void Parser::refuseDuplicateMember(const std::vector<Member> &members,
                                   std::size_t ownFrom, const std::string &name,
                                   const std::string &tag, std::size_t pos) {
    if (name.empty()) return;               // an anonymous union's own entry
    for (std::size_t i = ownFrom; i < members.size(); i++)
        if (members[i].name == name)
            src_.fail(pos, "'" + name + "' is declared twice in " +
                           (tag.empty() ? std::string("this class")
                                        : "'" + tag + "'") +
                           " - a class has one member of each name, and the "
                           "second would be a second place in the object that "
                           "nothing could name");
}

// At the `{` of a nameless union: is the token after its matching `}` a `;`?
bool Parser::anonymousUnionAhead() const {
    int depth = 0;
    for (std::size_t i = at_; i < tokens_.size(); i++) {
        if (tokens_[i].kind == TokenKind::End) return false;
        if (tokens_[i].is("{")) depth++;
        else if (tokens_[i].is("}") && --depth == 0)
            return i + 1 < tokens_.size() && tokens_[i + 1].is(";");
    }
    return false;
}

// A bit-field has no object to be a field of where the members are names of a scope.
void Parser::refuseAnonymousUnionShape(const Type *u, std::size_t pos) {
    const std::vector<Member> &ms = u->members();
    const std::vector<std::size_t> &where = anonUnionMemberPos_[u];
    for (std::size_t i = 0; i < ms.size(); i++)
        if (ms[i].isBitField())
            src_.fail(i < where.size() ? where[i] : pos,
                      "'" + ms[i].name + "' is a bit-field of an anonymous union outside a class, and "
                      "a bit-field there is not supported yet - it would be a name with no object to "
                      "be a field of; give the union a name");
}

// The storage's name, measured from clang: Itanium spells the first member as a mangled variable with
// no `L` (`_Z1a`, `_ZN1n1eE`); Microsoft numbers the object `$S<n>` of type `<unnamed-type-$S<n>>`.
std::string Parser::anonymousUnionSymbol(const Type *u, std::size_t pos) {
    const std::string hidden = "$S" + std::to_string(anonUnionSeq_);
    const std::string first = u->members().empty() ? hidden : u->members()[0].name;
    if (!target_.microsoftNames()) {
        const std::string q = namespacePrefix() + first;
        if (q.find("::") != std::string::npos) return itaniumDataName(q, false);
        return "_Z" + std::to_string(first.size()) + first;
    }
    Type *shape = types_.structType(Kind::Union, namespacePrefix() + "<unnamed-type-" + hidden + ">");
    shape->setLocalName("<unnamed-type-" + hidden + ">");
    if (!namespaceStack_.empty()) shape->setInNamespace();
    std::string out, why;
    if (!microsoftDataName(namespacePrefix() + hidden, shape, &out, &why))
        src_.fail(pos, "this anonymous union cannot be given a name the linker can hold: " + why);
    return out;
}

// **[class.union]/6: an anonymous union at namespace scope is `static`**, its members names of that scope
// sharing one object; in a block they are locals sharing one frame slot, or one static object.
void Parser::declareAnonymousUnionObject(const Type *u, StorageClass sc, std::size_t pos, bool local) {
    if (u->members().empty()) src_.fail(pos, "declaration does not declare anything");
    refuseAnonymousUnionShape(u, pos);
    const std::vector<Member> &ms = u->members();
    const std::vector<std::size_t> &where = anonUnionMemberPos_[u];
    anonUnionSeq_++;
    const std::string hidden = "$S" + std::to_string(anonUnionSeq_);
    auto redeclares = [&](std::size_t i) {
        src_.fail(i < where.size() ? where[i] : pos,
                  "member of anonymous union redeclares '" + ms[i].name + "'");
    };
    if (!local) {
        if (sc != StorageStatic && !inUnnamedNamespace_)
            src_.fail(pos, "anonymous unions at namespace or global scope must be declared 'static'");
        const std::string symbol = anonymousUnionSymbol(u, pos);
        for (std::size_t i = 0; i < ms.size(); i++) {
            const std::string gname = namespacePrefix() + ms[i].name;
            if (findGlobalToUpdate(gname) != nullptr) redeclares(i);
            globalIndex_[gname] = globals_.size();
            globals_.push_back(GlobalSym{ gname, symbol, ms[i].type, ms[i].type->isConst(), true, false, false, 0 });
        }
        current_->globals.push_back(Global{ hidden, symbol, u, std::vector<GlobalPiece>(), false, true, false });
        return;
    }
    const std::size_t from = scopeStarts_.empty() ? 0 : scopeStarts_.back();
    for (std::size_t i = 0; i < ms.size(); i++)
        for (std::size_t k = from; k < locals_.size(); k++)
            if (locals_[k].name == ms[i].name) redeclares(i);
    if (sc == StorageStatic) {
        const std::string symbol = uniqueStaticSymbol(ms.empty() ? hidden : ms[0].name);
        for (std::size_t i = 0; i < ms.size(); i++)
            declareStaticLocal(ms[i].name, ms[i].type, i < where.size() ? where[i] : pos, symbol);
        current_->globals.push_back(Global{ symbol, symbol, u, std::vector<GlobalPiece>(), false, true, false });
        return;
    }
    if (sc != StorageNone)
        src_.fail(pos, "an anonymous union in a block takes no storage class but 'static'");
    const int offset = declare(hidden, u, pos, 0);
    for (std::size_t i = 0; i < ms.size(); i++) {
        locals_.push_back(Local{ ms[i].name, offset, ms[i].type, false, std::string() });
        locals_.back().sharesSlot = true;
        locals_.back().serial = ++localSerial_;
        fnVars_.push_back(::Local{ ms[i].name, ms[i].type, offset, false, std::string(), currentBlock() });
        fnVars_.back().align = objectAlign(ms[i].type, target_);
    }
}

// Which anonymous unions of `cls` have a member among `names` - the mem-initialiser list's, or
// what a delegating constructor already built - so the rest of each is left alone.
std::set<const Type *> Parser::unionsNamedBy(const Type *cls, const std::set<std::string> &names) const {
    std::set<const Type *> out;
    const std::vector<Member> &ms = cls->members();
    for (std::size_t i = 0; i < ms.size(); i++)
        if (ms[i].anonUnion != nullptr && ms[i].declaredIn == nullptr && names.count(ms[i].name) != 0)
            out.insert(ms[i].anonUnion);
    return out;
}

bool Parser::podForLayout(const Type *t) const {
    if (t == nullptr) return true;
    const Type *u = t->unqualified();
    while (u->isArray()) u = u->pointee()->unqualified();
    if (!u->isStructOrUnion()) return true;      // a fundamental or a pointer
    // A vptr, a base, a constructor or a destructor each make it not a POD, and any
    // one of them is enough - the standard's list is longer, but the rest of it
    // cannot be written in this language yet.
    if (u->polymorphic()) return false;
    if (!u->bases().empty()) return false;
    if (!u->tag().empty()) {
        if (hasConstructors(u->tag())) return false;
        if (destructorOf(u) != nullptr) return false;
    }
    const std::vector<Member> &ms = u->members();
    for (std::size_t i = 0; i < ms.size(); i++)
        if (!podForLayout(ms[i].type)) return false;
    return true;
}

const Type *Parser::arraySuffix(const Type *base, std::size_t pos) {
    if (base->isReference() && peek().is("["))
        src_.fail(peek().pos, "there is no array of references - an array's "
                              "elements are objects, and a reference is not "
                              "one");
    std::vector<long long> dims;
    std::vector<const DepExpr *> expressed;
    // `[[` is an attribute after the bound, [dcl.attr.grammar]/6, and never a bound of its own.
    while (!(peek().is("[") && peekAt(1).is("[")) && consume("[")) {
        expressed.resize(dims.size() + 1, nullptr);
        if (consume("]")) { dims.push_back(-1); skipAttributes(); continue; }
        std::size_t dpos = peek().pos;
        // **`char (*)[sizeof(T) == 4]` while a signature is read as a pattern** - [temp.dep.constexpr]: a
        // bound over the parameters is kept as the expression, and read again with the arguments bound.
        if (patternOnly_ && !(peek().kind == TokenKind::Ident && peekAt(1).is("]")) && dependentAhead(false)) {
            const std::size_t from = at_;
            DepExpr tree = *dependentExpression(false, true);
            tree.text = depText(from, at_);
            expressed.back() = types_.intern(tree);
            dims.push_back(0);
            expect("]");
            skipAttributes();
            continue;
        }
        // **`T (&)[N]` while a signature is read for deduction**: a bare name that
        // is a pattern non-type parameter is a reference to it, as in `V<N>`, and
        // what deduceOne binds from the argument's length.
        if (patternOnly_ && peek().kind == TokenKind::Ident && peekAt(1).is("]") &&
            nonTypePatternParams_.count(peek().text) != 0) {
            dims.push_back(Type::parameterBound(nonTypePatternParams_[peek().text]));
            at_++;
            expect("]");
            skipAttributes();
            continue;
        }
        // `long long`, not `long`: this compiler is built by cl on one of its three
        // machines, where a `long` is 32 bits - so `char a[0x100000001]` silently
        // became `char a[1]` there and kept its full length on the other two.
        long long n = constantExpression("an array length");
        if (n <= 0)
            src_.fail(dpos, "an array length must be positive, not " +
                            std::to_string(n));
        dims.push_back(n);
        expect("]");
        skipAttributes();
    }
    for (std::size_t i = 1; i < dims.size(); i++)
        if (dims[i] == -1)
            src_.fail(pos, "only the first dimension may be left empty - the "
                           "others decide how far one step moves");
    // [dcl.array]/1: the element type is not void, a function type or an abstract class.
    if (!dims.empty()) {
        if (base->unqualified()->isVoid())
            src_.fail(pos, "there is no array of 'void' - [dcl.array]/1");
        if (base->isFunction())
            src_.fail(pos, "there is no array of functions - [dcl.array]/1; an array of pointers to "
                           "them is written 'R (*a[N])(params)'");
        if (base->unqualified()->isStructOrUnion() && base->unqualified()->abstract())
            src_.fail(pos, "there is no array of '" + base->unqualified()->describe() + "', an abstract "
                           "class - [dcl.array]/1 with [class.abstract]/3");
    }

    // **An object this compiler cannot measure is refused where it is written** - every size is a
    // signed 32-bit count, checked by division so the check cannot overflow. **Not in a pattern**,
    // whose `const T` no target can measure: the declaration is read again with its arguments.
    for (std::size_t i = dims.size(); i-- > 0; ) {
        const long long elem = patternOnly_ ? 0 : base->size(target_);
        if (dims[i] > 0 && elem > 0 && dims[i] > 2147483647LL / elem)
            src_.fail(pos, "this array is larger than this compiler can lay "
                           "out: " + std::to_string(dims[i]) + " elements of " +
                           std::to_string(elem) + " bytes is past the "
                           "2147483647 an object here is measured in");
        base = i < expressed.size() && expressed[i] != nullptr ? types_.dependentArrayOf(base, expressed[i])
                                                               : types_.arrayOf(base, dims[i]);
    }
    return base;
}

// **The name a conversion function is filed under names a class by its tag
// alone.**
static std::string conversionSpelling(const Type *t) {
    if (t->isConst() && t->unqualified() != t)
        return t->isPointer() ? conversionSpelling(t->unqualified()) + " const"
                              : "const " + conversionSpelling(t->unqualified());
    if (t->isStructOrUnion()) return t->tag();
    if (t->isPointer()) return conversionSpelling(t->pointee()) + " *";
    if (t->isReference())
        return conversionSpelling(t->referent()) +
               (t->isRValueReference() ? " &&" : " &");
    return t->describe();
}

// `operator` and then the operator itself, read where a declarator wants a name. What
// comes back is the whole of it - "operator+" - because that is the name the
// declaration carries on. **Everything this will not take, it refuses by name.**
std::string Parser::operatorName() {
    at_++;                                    // `operator`

    // The two that are written as a *pair* of tokens. `operator()` has to be
    // read here rather than left to the parameter list below, which would
    // take the `()` for an empty one and leave the declaration with no name.
    if (peek().is("(")) { at_++; expect(")"); return "operator()"; }
    if (peek().is("[")) { at_++; expect("]"); return "operator[]"; }

    const std::string spelling = peek().text;

    // The array forms: a class's own, which `new T[n]` and `delete[] p` reach
    // by [class.free]/2, or at namespace scope a replacement or the library's.
    if ((spelling == "new" || spelling == "delete") && peekAt(1).is("[")) {
        at_ += 2;
        expect("]");
        return "operator" + spelling + "[]";
    }
    // **[over.literal]/1: `operator "" _km`**, or `""_km` with the suffix run onto the quotes.
    if (peek().kind == TokenKind::Str) {
        const Token &q = peek();
        if (!q.text.empty() || q.prefix != 0 || q.wide)
            src_.fail(q.pos, "the string after 'operator' must be '\"\"' - [over.literal]/1 "
                             "names a literal operator by an empty string and a suffix");
        std::string suffix = q.udSuffix;
        at_++;
        if (suffix.empty()) {
            if (peek().kind != TokenKind::Ident)
                src_.fail(peek().pos, "a literal operator is named 'operator\"\"' and an "
                                      "identifier, its suffix - [over.literal]/1");
            suffix = peek().text;
            at_++;
        }
        return "operator\"\" " + suffix;
    }
    // **A type after `operator` is a conversion function**, not an operator
    // that happens to be spelled with letters - `new` and `delete` being the
    // two that are.
    if (peek().kind != TokenKind::Punct && spelling != "new" && spelling != "delete") {
        StorageClass csc;
        Qualifiers cq;
        const Type *to = specifiers(&csc, &cq);
        if (cq.isConst) to = types_.withConst(to);
        // **A conversion-declarator is only `*` and `&`** - [class.conv.fct]
        // and the grammar for conversion-type-id, which has no function and no
        // array declarator in it.
        for (;;) {
            if (consume("*")) {
                to = types_.pointerTo(to);
                while (peek().is("const") || peek().is("volatile")) {
                    to = peek().is("volatile") && sawVolatile() ? types_.withVolatile(to) : types_.withConst(to);
                    at_++;
                }
                continue;
            }
            if (peek().is("&") && !peekAt(1).is("&")) {
                at_++;
                to = types_.referenceTo(to);
                continue;
            }
            break;
        }
        conversionTarget_ = to;
        return "operator " + conversionSpelling(to);
    }
    if (findOperator(spelling) == nullptr)
        src_.fail(peek().pos, "'" + spelling + "' is not an operator, so "
                              "there is nothing here to overload");
    at_++;
    return "operator" + spelling;
}

// An operator this compiler can *name* but cannot yet reach from an expression,
// refused where it is declared. Which dispatch is missing depends on the parameter
// list, so it is asked once that is read. Accepting one leaves an uncallable function.
void Parser::checkOperatorDeclarable(const std::string &name,
                                     const std::vector<const Type *> &params,
                                     bool member, std::size_t pos,
                                     bool internal, const Type *returns) {
    if (!literalSuffix(name).empty()) {
        checkLiteralOperator(name, params, member, pos);
        return;
    }
    const std::string spelling = operatorSpelling(name);
    if (spelling.empty() || findOperator(spelling) == nullptr) return;
    // **An allocation function is an operator in name only** - [basic.stc.dynamic]:
    // `operator new` takes a size_t and `operator delete` a `void *`, member or
    // not, and nothing below applies to them.
    if (spelling == "new" || spelling == "delete" ||
        spelling == "new[]" || spelling == "delete[]") {
        if (!member && internal)
            src_.fail(pos, "'" + name + "' at namespace scope cannot be "
                           "static - [basic.stc.dynamic]/2 makes the "
                           "replacement the one the whole program calls");
        const bool sized = spelling == "new" || spelling == "new[]";
        const Type *first = params.empty() ? nullptr : params[0]->unqualified();
        const bool ok = first != nullptr &&
                        (sized ? first == types_.get(target_.sizeType())
                               : (first->isPointer() && first->pointee()->unqualified()->isVoid()));
        if (!ok)
            src_.fail(pos, "'" + name + "' must take " +
                           std::string(sized ? "a size_t" : "a 'void *'") +
                           " first - [basic.stc.dynamic.allocation]");
        // And give back `void *`, or nothing from a deallocation function - /1 of both clauses.
        if (returns != nullptr &&
            !(sized ? returns->isPointer() && returns->pointee() == types_.get(Kind::Void)
                    : returns == types_.get(Kind::Void)))
            src_.fail(pos, "'" + name + "' must return " +
                           std::string(sized ? "'void *'" : "'void'") + ", and this returns '" +
                           returns->describe() + "' - [basic.stc.dynamic." +
                           (sized ? "allocation" : "deallocation") + "]/1");
        // The library's placement pair - `(size_t, void *)`, `(void *, void
        // *)` - may be declared, as <new> declares it; `new (p) T` builds at
        // p without calling it, which is what that pair does.
        if (params.size() == 2 && !member) {
            const Type *second = params[1]->unqualified();
            if (second->isPointer() && second->pointee()->unqualified()->isVoid()) return;
            // And the nothrow pair, `(size_t, const std::nothrow_t &)`, which <new> declares.
            if (second->isReference() && second->pointee()->unqualified()->isStructOrUnion() &&
                second->pointee()->unqualified()->tag() == "std::nothrow_t") return;
        }
        return;  // further parameters are placement ones ([expr.new]/14, /20)
    }

    // **[over.oper]/6: a non-member operator needs a class or an enumeration
    // among its parameters**, or a reference to one.
    if (!member && !params.empty()) {
        bool overClass = false;
        for (std::size_t k = 0; k < params.size(); k++) {
            const Type *p = params[k];
            if (p->isReference()) p = p->referent();
            p = p->unqualified();
            if (p->isStructOrUnion() || p->isEnumeration()) overClass = true;
        }
        if (!overClass)
            src_.fail(pos, "'" + name + "' takes no parameter of class or "
                           "enumeration type, and [over.oper] asks every "
                           "operator function for one - an operator over "
                           "built-in types already has its meaning, and this "
                           "declaration could never be called");
    }

    // `this` is the first operand of a member operator and is not in the list.
    const std::size_t operands = params.size() + (member ? 1 : 0);

    // [over.oper]/8: no default arguments but on the call operator; [over.inc]/1: postfix's extra one is an int.
    if (spelling != "()")
        for (std::size_t i = 0; i < pendingDefaults_.size(); i++)
            if (pendingDefaults_[i] != 0)
                src_.fail(pos, "'" + name + "' cannot have a default argument - [over.oper]/8 allows one "
                               "on 'operator()' alone");
    if (operands == 2 && (spelling == "++" || spelling == "--") &&
        params.back()->unqualified()->kind() != Kind::Int)
        src_.fail(pos, "the parameter of a postfix '" + name + "' must be of type 'int', and this is '" +
                       params.back()->describe() + "' - [over.inc]/1");

    static const char *const binary[] = {
        "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
        "==", "!=", "<", "<=", ">", ">=", "&&", "||", ",", "->*"
    };
    if (operands == 2)
        for (const char *k : binary)
            if (spelling == k) return;

    static const char *const unary[] = {
        "+", "-", "*", "&", "!", "~", "++", "--"
    };
    if (operands == 1)
        for (const char *k : unary)
            if (spelling == k) return;

    // **The postfix increment is the one operator whose arity lies.**
    if (operands == 2 && (spelling == "++" || spelling == "--")) return;

    // **The compound assignments, `@=`, member or not.** [over.ass]/1 restricts
    // plain `=` to a member and says nothing about these; [over.binary]/1 lets
    // one be a non-member of two parameters, `operator*=(Q &, const Q &)`.
    static const char *const compound[] = {
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
    };
    if (operands == 2)
        for (const char *k : compound)
            if (spelling == k) return;

    // **[over.sub]: subscripting is a member and takes exactly one argument.**
    if (spelling == "[]" && operands == 2 && member) return;

    // **[over.ass]**.
    if (spelling == "=" && operands == 2 && member) return;

    // **[over.ref]: `operator->` is a member and takes nothing.**
    if (spelling == "->" && operands == 1 && member) return;

    // The call operator has no arity to check: [over.call] lets it take
    // whatever it likes, and it has no non-member form to be confused with.
    if (spelling == "()" && member) return;
    if (spelling == "()")
        src_.fail(pos, "'operator()' has to be a non-static member function - "
                       "[over.call] gives it no non-member form, so there is no "
                       "class here for it to be the call operator of");

    const std::string how = operands == 1 ? "a unary " : "a binary ";
    src_.fail(pos, how + "'operator" + spelling + "' is not supported yet - "
                   "it can be given the name the linker wants, and there is no "
                   "path from an expression to it, so declaring one would make "
                   "a function nothing can call");
}

void Parser::checkLiteralOperator(const std::string &name,
                                  const std::vector<const Type *> &params,
                                  bool member, std::size_t pos) {
    const std::string shown = "operator\"\"" + literalSuffix(name.substr(name.rfind(':') + 1));
    if (member)
        src_.fail(pos, "literal operator '" + shown + "' must be in a namespace or global "
                       "scope - [over.literal]/2");
    if (cLinkage_ > 0)
        src_.fail(pos, "literal operator must have C++ linkage - [over.literal]/6");
    for (std::size_t i = 0; i < pendingDefaults_.size(); i++)
        if (pendingDefaults_[i] != 0)
            src_.fail(pos, "literal operator cannot have a default argument - [over.literal]/8");
    // A template's specialization takes nothing, the template's own list being checked where it is declared.
    if (params.empty() && !instantiationKey_.empty()) return;
    auto character = [&](const Type *t) {
        const Kind k = t->unqualified()->kind();
        return k == Kind::Char || k == Kind::WChar || k == Kind::Char16 || k == Kind::Char32;
    };
    bool ok = false;
    if (params.size() == 1) {
        const Type *p = params[0]->unqualified();
        ok = p->kind() == Kind::ULongLong || p->kind() == Kind::LongDouble || character(p) ||
             (p->isPointer() && p->pointee()->isConst() &&
              p->pointee()->unqualified()->kind() == Kind::Char);
    } else if (params.size() == 2) {
        const Type *p = params[0]->unqualified();
        ok = p->isPointer() && p->pointee()->isConst() && character(p->pointee()) &&
             params[1]->unqualified() == types_.get(target_.sizeType());
    }
    if (!ok)
        src_.fail(pos, "invalid literal operator parameter list for '" + shown + "' - "
                       "[over.literal]/3 takes (const char *), (unsigned long long), (long "
                       "double), one character type, or a pointer to const characters and a "
                       "size_t");
}

void Parser::checkLiteralOperatorTemplate(const TemplateDecl &decl, const Type *pattern) {
    const bool chars = decl.params.size() == 1 && decl.params[0].isPack &&
                       decl.params[0].type != nullptr && !decl.params[0].isTemplate &&
                       decl.params[0].type->unqualified()->kind() == Kind::Char;
    if (!chars)
        src_.fail(decl.pos, "template parameter list for literal operator must be 'char...' - "
                            "[over.literal]/5");
    if (pattern != nullptr && !pattern->params().empty())
        src_.fail(decl.pos, "literal operator template cannot have any parameters - "
                            "[over.literal]/5");
}

// `expectIdent` with the operator case in front of it. This stands wherever a
// declarator reads a name, which is three places: the plain one, the one
// after a `::`, and the one after a class template's argument list.
std::string Parser::declaredName(const char *what) {
    if (peek().is("operator")) return operatorName();
    return expectIdent(what);
}

Parser::Declared Parser::declarator(const Type *base, bool nameOptional,
                                    bool insideParens) {
    skipAttributes();

    // The const after a star qualifies the pointer, not what it points at: `char *
    // const p` is a const pointer to a writable char and `const char *p` the other
    // way round. Both are differences of type, so the declarator remembers neither.
    while (consume("*")) {
        base = types_.pointerTo(base);
        for (;;) {
            if (skipAttributes()) continue;
            if (consume("const"))    { base = types_.withConst(base); continue; }
            if (consume("volatile")) { sawVolatile(); base = types_.withVolatile(base); continue; }
            break;
        }
    }

    // A reference binds after every star - `int *&r` is a reference to a pointer -
    // and there is nothing on the other side of it, a reference being no object to
    // point at. **`&&` binds like `&`**, differing only in what it will take.
    if ((peek().is("&") || peek().is("&&")) && base->unqualified()->isVoid())
        src_.fail(peek().pos, "cannot form a reference to 'void' - [dcl.ref]/1");
    if (consume("&&")) {
        skipAttributes();
        base = base->isReference() ? base : types_.rvalueReferenceTo(base);  // [dcl.ref]/6 collapsing
        if (peek().is("&") || peek().is("&&"))
            src_.fail(peek().pos, "there is no reference to a reference");
        if (peek().is("*"))
            src_.fail(peek().pos, "there is no pointer to a reference");
        return declarator(base, nameOptional, insideParens);
    }
    if (consume("&")) {
        skipAttributes();
        base = types_.referenceTo(base->isReference() ? base->referent() : base);
        if (peek().is("&") || peek().is("&&"))
            src_.fail(peek().pos, "there is no reference to a reference");
        if (peek().is("*"))
            src_.fail(peek().pos, "there is no pointer to a reference - a "
                                  "reference is not an object to point at");
        // [dcl.ref]/1: there is no const reference, only a reference to a
        // const. The distinction is worth keeping because the two are written
        // so nearly the same way.
        if (peek().is("const") || peek().is("volatile"))
            src_.fail(peek().pos, "a reference cannot be const or volatile "
                                  "itself - it never changes what it refers to "
                                  "anyway; 'const " +
                                  base->referent()->unqualified()->describe() +
                                  " &' is what qualifies what it refers to");
    }

    // [dcl.name]: with the name left out, a parenthesis inside a parenthesised declarator may open the parameter
    // list the name would have stood before - `int (*(double))(double)`, a function taking a double that returns a
    // pointer to a function - and it does where a parameter can start: at `)`, at `...`, at a type. (Until 1.75 it
    // was read as one more parenthesised declarator, and such a type-id was "expected )".) The same after a
    // pointer operator outside any parenthesis: `int *(int)` and `const char *()` are functions returning a
    // pointer (1.80: `std::is_same<decltype(f), const char *()>` read its second argument as a pointer).
    bool parametersOfNoName = false;
    if (peek().is("(") && nameOptional && (insideParens || base->isPointer()) && !atParenDeclarator()) {
        if (peekAt(1).is(")") || peekAt(1).is("...")) parametersOfNoName = true;
        else {
            const std::size_t back = at_;
            at_++;
            parametersOfNoName = atTypeName();
            at_ = back;
        }
    }
    if (peek().is("(") && !parametersOfNoName) {
        std::size_t open = at_;
        const Nesting deep(this, peek().pos, "parenthesised declarator");
        at_++;
        // `int (*p)()` and `int (S::*p)()` are the same shape to this branch: what is
        // inside the parentheses points at something, so what follows them is a parameter
        // list, not an array bound. The class may be nested: `(Nest::In::*m)` (A20).
        std::size_t q = 0;
        while (peekAt(q).kind == TokenKind::Ident) {
            std::size_t r = q + 1;
            // `int (Box<int>::*f)()`: a template-id's arguments are passed over to its `::`.
            if (peekAt(r).is("<")) {
                int angle = 0;
                for (; at_ + r < tokens_.size(); r++) {
                    const Token &t = peekAt(r);
                    if (angle > 0 && (t.is("(") || t.is("["))) { r = groupCloseAt(at_ + r) - at_; continue; }
                    if (t.is("<")) angle++;
                    else if (t.is(">")) angle--;
                    else if (t.is(">>")) angle -= 2;
                    else if (t.is(";") || t.is("{") || t.kind == TokenKind::End) break;
                    if (angle <= 0) break;
                }
                if (angle != 0) break;
                r++;
            }
            if (!peekAt(r).is("::")) break;
            q = r + 1;
        }
        const bool wrapsMemberPointer = q > 0 && peekAt(q).is("*");
        // `int (&f)(int)` likewise: a reference to a function, its parameter list outside.
        bool wrapsAPointer = peek().is("*") || peek().is("&") || peek().is("&&") ||
                             wrapsMemberPointer;

        // **To the matching ')' by counting brackets**: reading the inner
        // declarator twice doubled per level (2^N for `int (((x)))`); one that
        // never closes is read the old way, refused where and as it was.
        std::size_t close = open;
        int depth = 0;
        for (; close < tokens_.size() && tokens_[close].kind != TokenKind::End; close++) {
            const Token &t = tokens_[close];
            if (t.is("(") || t.is("[") || t.is("{")) depth++;
            else if (t.is(")") || t.is("]") || t.is("}")) { if (--depth == 0) break; }
        }
        if (depth == 0 && close < tokens_.size() && tokens_[close].is(")")) at_ = close;
        else declarator(types_.intType(), true, true);
        expect(")");

        std::size_t posOuter = peek().pos;
        const Type *outer;
        if (peek().is("(") && wrapsAPointer) {
            std::vector<const Type *> params;
            bool variadic = false;
            parameterTypes(params, variadic);
            // **`int (S::*f)() const` - [dcl.fct]/6: the cv-qualifier-seq is the function
            // type's**, so the pointer holds a const member and is called on a const object.
            unsigned cv = 0;
            while (wrapsMemberPointer && (peek().is("const") || peek().is("volatile"))) {
                if (consume("const")) cv |= Type::kThisConst;
                else if (consume("volatile") && sawVolatile()) cv |= Type::kThisVolatile;
            }
            if (!wrapsMemberPointer && (peek().is("const") || peek().is("volatile")))
                src_.fail(peek().pos, "a pointer or reference to a function type cannot "
                                      "have a '" + peek().text + "' qualifier - only a "
                                      "non-static member function can, [dcl.fct]/6");
            // `int (S::*)() &` - the ref-qualifier is the function type's, [dcl.fct]/6; a
            // pointer or reference to a non-member function type may not carry one.
            if (!wrapsMemberPointer && (peek().is("&") || peek().is("&&")))
                src_.fail(peek().pos, "a pointer or reference to a function type cannot "
                                      "have a '" + peek().text + "' qualifier - only a "
                                      "non-static member function can, [dcl.fct]/6");
            const RefQual ref = refQualifier();
            fnPointerSpec_ = skipExceptionSpecification();
            outer = types_.functionType(base, std::move(params), variadic, ref, cv);
        } else {
            outer = arraySuffix(base, posOuter);
        }
        std::size_t after = at_;

        at_ = open + 1;
        Declared inner = declarator(outer, nameOptional, true);
        expect(")");
        at_ = after;
        return inner;
    }

    std::size_t pos = peek().pos;
    std::string name;
    std::string qualifier;

    // Replaying `~X() { ... }` from inside the class: the '~' belongs to the
    // name, and there is no '::' to hang it off.
    bool inlineDtor = false;
    if (!inlineOwner_.empty() && peek().is("~")) { at_++; inlineDtor = true; }

    // The operator test comes before the optional-name one: an abstract
    // declarator never says `operator`, so reaching it here is always a name.
    if (peek().is("operator")) name = operatorName();
    else if (nameOptional && peek().kind != TokenKind::Ident) name.clear();
    else name = expectIdent("a name");

    // **`Box<T, N>::size` - a class template's name where a class name goes.** The
    // name just read is a class template, so what follows it is an argument list and
    // the class it makes is the qualifier. The rest is read by the loop below.
    {
        auto tmpl = findTemplate(name);
        if (!name.empty() && peek().is("<") && tmpl != templates_.end() &&
            tmpl->second.isClass) {
            const std::size_t tpos = pos;
            const Type *cls = instantiateClass(tmpl->second, tpos);
            // A held `V<N>(` or `~V<N>(` replayed: the class's own name, not a qualifier.
            const bool own = !inlineOwner_.empty() && name == inlineOwnerName_ &&
                             cls->unqualified()->tag() == inlineOwner_ && peek().is("(");
            if (!own) {
                if (!peek().is("::"))
                    src_.fail(peek().pos, "'" + cls->tag() + "' is a type here, "
                                          "and a declaration needs a name after "
                                          "it");
                if (peekAt(1).is("*")) {
                    // `int Box<int>::*p`: the loop below reads `::*` after the class's own name.
                    qualifier.clear();
                    name = cls->tag();
                } else {
                    at_++;
                    qualifier = cls->tag();
                    name = declaredName("a member name");
                }
            }
        }
    }

    if (!inlineOwner_.empty() && !name.empty() && !peek().is("::")) {
        qualifier = inlineOwner_;
        // **A specialization's constructor is written under the template's name and
        // keyed under the tag's.** The source says `Holder(`; the table says
        // "Holder<int>::Holder<int>". Only the name that *is* the class moves.
        if (name == inlineOwnerName_ && inlineOwnerName_ != inlineOwner_)
            name = localOf(inlineOwner_);
        if (inlineDtor) name = "~" + name;
        inlineOwnerName_.clear();
        inlineOwner_.clear();     // one-shot: the body's own declarations are
                                  // ordinary locals, not members
    }

    // `int Point::get()`.
    while (!name.empty() && peek().is("::")) {
        // **`int S::*p` - a pointer to a member of S.** The `::` is not qualifying a
        // name being defined; what follows it is a star. The declarator restarts from
        // that star with the member-pointer type in hand, which covers every shape.
        if (peekAt(1).is("*")) {
            const std::string of = qualifier.empty() ? name
                                                     : qualifier + "::" + name;
            // `M C::*` with C a template parameter is a pattern, deduced from `&S::m` (R15).
            const Type *cls = findTypedef(of);
            if (cls == nullptr || !(cls->isStructOrUnion() || cls->kind() == Kind::TemplateParam))
                src_.fail(pos, "'" + of + "' is not a class, so '" + of +
                               "::*' names no member of anything");
            at_ += 2;                              // the '::' and the '*'
            // cl sizes a member pointer by the class's inheritance model, and an
            // incomplete class takes the three-field "unspecified" form: refused.
            // Not the class being defined: its bases are read, and the pointer's size is asked for when the
            // class is complete - `int Outer::*pick();` as a member of Outer (1.79: refused with the rest).
            bool beingDefined = currentClass_ != nullptr && currentClass_->unqualified() == cls->unqualified();
            for (const Type *open : classStack_)
                beingDefined = beingDefined || (open != nullptr && open->unqualified() == cls->unqualified());
            if (target_.microsoftNames() && !cls->isComplete() && !beingDefined)
                src_.fail(pos, "'" + of + "::*' before '" + of + "' is complete - "
                               "the Microsoft ABI sizes a pointer to a member by "
                               "the class's inheritance model, and an incomplete "
                               "class takes the widest form (cl's unspecified "
                               "model, 12 and 24 bytes). Not supported yet; "
                               "define the class first");
            const Type *mp;
            // [dcl.mptr]/3: a pointer to member does not point at a member of reference type, nor of 'void'.
            if (base->isReference() || base->unqualified()->isVoid())
                src_.fail(pos, "'" + base->describe() + " " + of + "::*' - a pointer to member cannot point "
                               "at a member of type '" + base->describe() + "', [dcl.mptr]/3");
            if (base->isFunction()) {
                mp = types_.memberFunctionPointerTo(cls, base, target_);
            } else {
                mp = types_.memberPointerTo(cls, base);
            }
            for (;;) {
                if (consume("const"))    { mp = types_.withConst(mp); continue; }
                if (consume("volatile")) { sawVolatile(); mp = types_.withVolatile(mp); continue; }
                break;
            }
            return declarator(mp, nameOptional, insideParens);
        }
        at_++;
        qualifier = qualifier.empty() ? name : qualifier + "::" + name;
        bool destructor = consume("~");
        name = declaredName("a member name after '::'");
        if (destructor) name = "~" + name;
        // `int n::B2<int>::*p`: a namespace's class template with its arguments, then `::*` after the class.
        if (!destructor && peek().is("<")) {
            auto tmpl = findTemplate(qualifier + "::" + name);
            if (tmpl != templates_.end() && tmpl->second.isClass) {
                const std::size_t back = at_;
                const Type *cls = instantiateClass(tmpl->second, pos);
                if (peek().is("::") && peekAt(1).is("*")) {
                    qualifier.clear();
                    name = cls->tag();
                } else {
                    at_ = back;
                }
            }
        }
    }
    // `int b::f()` inside `namespace a`: the qualifier's first name is found innermost first, `a::b`.
    if (!qualifier.empty() && !namespaces_.count(qualifier)) {
        const std::size_t cut = qualifier.find("::");
        const std::string head = qualifier.substr(0, cut);
        const std::string ns = namespaceNamed(head);
        if (!ns.empty() && ns != head)
            qualifier = ns + (cut == std::string::npos ? std::string() : qualifier.substr(cut));
    }

    skipAttributes();
    // `int D::modes_[Count]`: the bound after a class's declarator-id is in its scope.
    const Type *declaredIn = qualifier.empty() ? nullptr : findTypedef(qualifier);
    const Type *t;
    if (patternOnly_ && !qualifier.empty() && peek().is("[")) {
        // `C<T>::arr[Count]` read as a pattern: the class has no members to fold the
        // bound from, and only the name is asked - its own definition reads it again.
        while (consume("[")) {
            for (int depth = 1; depth > 0; at_++) {
                if (peek().kind == TokenKind::End) src_.fail(peek().pos, "expected ']'");
                if (peek().is("[")) depth++;
                else if (peek().is("]")) depth--;
            }
        }
        t = types_.arrayOf(base, -1);
    } else {
        const StaticMemberScope scope(this, declaredIn != nullptr && declaredIn->isStructOrUnion() &&
                                            peek().is("[") ? declaredIn : nullptr);
        // `int ns::f(S s)`: what follows the declarator-id is looked up in ns, [basic.lookup.unqual]/8.
        struct Restore {
            std::vector<std::string> &stack; const std::vector<std::string> saved;
            ~Restore() { stack = saved; }
        } restore{ namespaceStack_, namespaceStack_ };
        if (declaredIn == nullptr && !qualifier.empty() && namespaces_.count(qualifier)) {
            namespaceStack_.clear();
            for (std::size_t s = 0, e; s <= qualifier.size(); s = e + 2) {
                e = qualifier.find("::", s);
                if (e == std::string::npos) e = qualifier.size();
                namespaceStack_.push_back(qualifier.substr(s, e - s));
            }
        }
        t = arraySuffix(base, pos);
    }
    skipAttributes();

    std::size_t paramsAt = 0;
    if ((insideParens || (parametersOfNoName && name.empty())) && peek().is("(")) {
        paramsAt = at_;
        std::vector<const Type *> ignored;
        bool ignoredVariadic = false;
        parameterTypes(ignored, ignoredVariadic);
        // A name-less one has no declaration to make its function type from the list: it is made here.
        if (parametersOfNoName && name.empty()) {
            t = types_.functionType(t, std::move(ignored), ignoredVariadic);
            paramsAt = 0;
        }
    }

    return Declared{ name, t, pos, paramsAt, qualifier };
}


RefQual Parser::refQualifier() {
    if (consume("&"))  return RefQual::LValue;
    if (consume("&&")) return RefQual::RValue;
    return RefQual::None;
}

// **`V<N>(` and `~V<N>(` in V<N>'s own body name its constructor and destructor**:
// the injected-class-name with its own arguments, [temp.local]/1. C++11 takes it, C++20
// does not. Asked only of a specialization, and its arguments must make this very class.
bool Parser::atOwnTemplateId(const std::string &tag, const std::string &local) {
    if (peek().kind != TokenKind::Ident || peek().text != local || !peekAt(1).is("<") ||
        tag.find('<') == std::string::npos)
        return false;
    auto tmpl = findTemplate(local);
    if (tmpl == templates_.end() || !tmpl->second.isClass) return false;
    const std::size_t save = at_;
    at_++;
    const Type *cls = instantiateClass(tmpl->second, peek().pos);
    if (cls != nullptr && cls->unqualified()->tag() == tag && peek().is("(")) return true;
    at_ = save;
    return false;
}

// `V<...>(` ahead: a name, an argument list whose angles balance, then a parenthesis.
bool Parser::ownArgsThenParen() const {
    if (!peekAt(1).is("<")) return false;
    int depth = 0;
    for (std::size_t i = 1;; i++) {
        const Token &t = peekAt(i);
        if (t.kind == TokenKind::End || t.is(";") || t.is("{")) return false;
        if (depth > 0 && (t.is("(") || t.is("["))) { i = groupCloseAt(at_ + i) - at_; continue; }
        if (t.is("<")) depth++;
        else if (t.is(">")) depth--;
        else if (t.is(">>")) depth -= 2;
        if (depth <= 0) return depth == 0 && peekAt(i + 1).is("(");
    }
}
