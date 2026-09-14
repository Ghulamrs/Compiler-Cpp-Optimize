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
    while (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        const std::string member = peekAt(1).text;
        if (patternOnly_ &&
            (t->kind() == Kind::TemplateParam || t->kind() == Kind::DependentMember ||
             (t->isStructOrUnion() && t->isSpecialization()))) {
            at_ += 2;
            t = types_.dependentMember(t, member);
            continue;
        }
        if (!t->isStructOrUnion()) break;
        const Type *found = lookupInClass(t, member);
        // **C++11 lets `sizeof(S::m)` name a non-static data member with no object** -
        // [expr.sizeof]/2 - so say which of the two this is: a member that exists and is not a type
        // reads differently from a name the class does not have at all.
        if (found == nullptr && t->findMember(member) != nullptr)
            src_.fail(peekAt(1).pos, "naming the non-static data member '" +
                                     member + "' without an object is C++11 "
                                     "and is not supported yet - 'sizeof' an "
                                     "object of the class, or its type");
        if (found == nullptr)
            src_.fail(peekAt(1).pos, "'" + t->tag() + "' has no member type "
                                     "called '" + member + "'");
        at_ += 2;
        t = found;
    }
    return t;
}

const Type *Parser::structOrUnionSpecifier(Kind kind, bool isClass) {
    const char *what = isClass ? "class" : (kind == Kind::Struct ? "struct" : "union");
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

    std::string tag;
    if (peek().kind == TokenKind::Ident) { tag = peek().text; at_++; }

    // **`final` on a class head forbids deriving from it** - [class]/3 - which
    // is a check made at every later derivation rather than anything about
    // this one, and nothing records it.
    if (peek().is("final") && (peekAt(1).is("{") || peekAt(1).is(":")))
        src_.fail(peek().pos, "'final' on a class is not supported yet: "
                              "nothing here records that a class may not be "
                              "derived from, so the word would be accepted "
                              "and never checked");

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

    if (within != nullptr && !tag.empty()) {
        if (!defining) {
            if (const Type *had = findTypedef(tag))
                if (had->isStructOrUnion()) return had;
        }
        tag = within->tag() + "::" + tag;
    }

    // **[class.local]: a class defined in a function body belongs to that function**,
    // so its tag carries the function's name, with a counter where two overloads
    // write the same one. A specialization is not local even where a function asked.
    bool inNamespace = false;
    if (within == nullptr && !tag.empty() && !namespaceStack_.empty() &&
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
        // **The injected class name.** Inside `Holder`'s own body the word `Holder`
        // means this specialization and not the template, which is what makes
        // `const Holder &` a legal parameter there. Registered as a member type.
        declareTypeName(tag + "::" + specializationOf, type);
    }
    // A class in a namespace: the manglers want the name without the
    // namespaces, and there is no enclosing type to point at - a namespace is
    // not a Type, which is the whole reason its scopes ride in the tag.
    if (inNamespace) { type->setLocalName(local); type->setInNamespace(); }
    if (within != nullptr && !local.empty()) {
        type->setLocalName(local);
        type->setEnclosing(within);
    }
    // A definition decides this, and a declaration answers when no definition
    // has.
    if (peek().is("{") || peek().is(":")) type->setDeclaredClass(isClass);
    else                                  type->noteClassKey(isClass);
    if (!localOwner.empty()) {
        // The single component is what both ABIs spell inside the wrapper, and the
        // written name is what resolves inside this function - which also shadows a
        // global class of the same name, the local scope being asked first.
        type->setLocalName(local);
        type->setLocalOwner(localOwner);
        localClassOwner_[tag] = localOwner;
        localTypes_[local] = type;
    }
    if (!tag.empty()) declareTypeName(tag, type);

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

            std::size_t bpos = peek().pos;
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
            if (b == nullptr || !b->isStructOrUnion())
                src_.fail(bpos, "'" + baseName + "' is not a class, so it "
                                "cannot be a base");
            if (!b->isComplete())
                src_.fail(bpos, "'" + baseName + "' is not defined yet - a base "
                                "class has to be complete, because the derived "
                                "object contains one");
            written.push_back(WrittenBase{ b, how, isVirtualBase });
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

    // **The base subobject is laid down first, at offset 0**, its members
    // copied in at the offsets they already have - that is what the layout IS,
    // so `d.b` needs no second search. Access travels through the inheritance.
    const Type *primary = nullptr;
    if (!target_.microsoftNames())
        for (std::size_t bi = 0; bi < written.size() && primary == nullptr; bi++)
            if (!written[bi].isVirtual && written[bi].type->hasVptr())
                primary = written[bi].type;
    type->setPrimaryBase(primary);
    std::vector<std::size_t> layOrder;
    for (std::size_t bi = 0; bi < written.size(); bi++)
        if (!written[bi].isVirtual && written[bi].type == primary)
            layOrder.push_back(bi);
    for (std::size_t bi = 0; bi < written.size(); bi++)
        if (!written[bi].isVirtual && written[bi].type != primary)
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

    for (std::size_t li = 0; li < layOrder.size(); li++) {
        // A virtual base is not in that order at all: it goes after every
        // non-virtual byte, which is not known until the members have been read.
        const std::size_t bi = layOrder[li];
        const Type *b = written[bi].type;
        const Access how = written[bi].access;
        // **cl lays every base with a vfptr before the rest**, wherever it was
        // written - `struct PX : E1, PV` has PV at 0 - which is not laid here. The
        // vtable code refuses it only when the class adds a virtual of its own.
        if (target_.microsoftNames() && bi != 0 && b->polymorphic())
            src_.fail(pos, "'" + tag + "' has virtual functions in a base "
                           "that is not the first, and the Microsoft ABI "
                           "lays that out differently - the base with the "
                           "vfptr goes first, wherever it was written. Not "
                           "supported yet; it is measured for Itanium only");

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
            members.push_back(m);
        }
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
        const bool atZero = primary != nullptr ? b == primary : bi == 0;
        if (!tag.empty() && atZero && b->polymorphic()) {
            vtables_[tag] = vtables_[b->tag()];
            ownSlotsFrom_[tag] = vtables_[tag].size();
        }
    }
    // Recorded in the order written, whatever order they were laid in: the
    // constructor and the destructor walk this list.
    for (std::size_t bi = 0; bi < written.size(); bi++)
        if (!written[bi].isVirtual)
            type->addBase(written[bi].type, baseAt[bi], written[bi].access);

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
            mt.afterParams = at_;
            mt.pos = peek().pos;
            if (peek().is("struct") || peek().is("class") || peek().is("union"))
                src_.fail(peek().pos, "a member class template is not supported "
                                      "yet - only member function templates are");
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
                    else
                        mname = "operator" + tokens_[i + 1].text;
                    break;
                }
                if (depth == 0 && t.is("(")) break;
                if (depth == 0 && t.is(";")) break;
                if (t.is("<") || t.is("(") || t.is("[")) depth++;
                else if (t.is(">") || t.is(")") || t.is("]")) depth--;
                else if (depth == 0 && t.kind == TokenKind::Ident) mname = t.text;
            }
            if (mname.empty())
                src_.fail(mt.pos, "a member function template needs a name");
            mt.name = mname;
            mt.memberAccess = access;
            mt.ownerTag = tag;
            mt.ownerType = nullptr;    // filled at instantiation from the tag
            // A member of a class template: capture the class's own parameters
            // and this instantiation's binding, so both layers bind at replay.
            mt.classParams = instantiatingParams_;
            mt.classBinding = instantiatingBinding_;
            mt.classValues = instantiatingValues_;
            at_ = mt.afterParams;
            mt.defined = skipTemplatedDefinition();
            if (!mt.defined)
                src_.fail(mt.pos, "a member function template declared inside "
                                  "its class must be defined there too - an "
                                  "out-of-line member template is not supported "
                                  "yet");
            memberTemplates_[tag + "::" + mname] = mt;
            continue;
        }

        // **A using-declaration in a class is a different rule from one at namespace scope**, which
        // this compiler has: here it redeclares a base member, changing its access or bringing an
        // overload set into the derived class's own, and neither is an alias.
        refuseAliasDeclaration();
        if (peek().is("using"))
            src_.fail(peek().pos, "a using-declaration inside a class is not "
                                  "supported yet - it redeclares a base member "
                                  "here rather than naming it, which changes "
                                  "access and overload resolution; one at "
                                  "namespace scope works");

        // Where this member's declaration begins.
        std::size_t itemStart = at_;

        // Set when a body was held and stepped over.
        bool heldBody = false;

        // **`explicit`, and the replay must not start at it** - the same rule
        // `virtual` follows a few lines down: a held body is re-read through the
        // out-of-line path, where C++ does not write the keyword.
        bool isExplicit = false;
        const std::size_t explicitAt = peek().pos;
        if (peek().is("explicit")) { isExplicit = true; at_++; itemStart = at_; }

        // **A `constexpr` constructor makes a literal type** - one whose
        // objects a constant expression may build.
        if (!tag.empty() && peek().is("constexpr") &&
            peekAt(1).kind == TokenKind::Ident && peekAt(1).text == local &&
            peekAt(2).is("("))
            src_.fail(peek().pos, "a 'constexpr' constructor is not supported "
                                  "yet: the constant evaluator folds a call to "
                                  "a function and has no object to build, so a "
                                  "'constexpr' object of a class type cannot be "
                                  "made here");

        // A constructor has the class's own name and no return type, so it
        // has to be seen before specifiers() is asked for one - the name is a
        // registered type name by now and would be read as the type.
        if (!tag.empty() && peek().kind == TokenKind::Ident &&
            peek().text == local && peekAt(1).is("(")) {
            std::size_t cpos = peek().pos;
            at_++;
            const std::size_t sigAt = functions_.size();
            declareConstructor(tag, cpos, access, isExplicit);
            isExplicit = false;
            if (peek().is("{") || peek().is(":")) {
                pendingBodies_.push_back(PendingBody{
                    tag, itemStart, local, constructorKey(tag),
                    signatureAddedUnder(constructorKey(tag), sigAt) });
                skipBracedBlock();
                continue;
            }
            refuseDefaultedOrDeleted();
            expect(";");
            continue;
        }

        // **`explicit` on a conversion function is C++11 and is not built.**
        if (isExplicit && peek().is("operator") &&
            peekAt(1).kind != TokenKind::Punct) {
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
        if (!tag.empty() && peek().is("~") && peekAt(1).kind == TokenKind::Ident &&
            peekAt(1).text == local && peekAt(2).is("(")) {
            std::size_t dpos = peek().pos;
            at_ += 2;
            const std::size_t sigAt = functions_.size();
            declareDestructor(tag, dpos, access, isVirtual);
            if (peek().is("{")) {
                pendingBodies_.push_back(PendingBody{
                    tag, itemStart, local, destructorKey(tag),
                    signatureAddedUnder(destructorKey(tag), sigAt) });
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
            if (peek().is("class") || peek().is("struct") || peek().is("union"))
                src_.fail(fpos, "'friend class' is not supported yet - it "
                                "grants every member function of another class "
                                "access at once, where this grants one named "
                                "function");
            StorageClass fsc;
            Qualifiers fquals;
            const Type *fbase = specifiers(&fsc, &fquals);
            if (fsc != StorageNone)
                src_.fail(fpos, "a friend declaration takes no storage class - "
                                "the function it names is somebody else's");
            Declared fd = declarator(fbase);
            if (!fd.qualifier.empty())
                src_.fail(fd.pos, "befriending one member function of another "
                                  "class is not supported yet - '" +
                                  fd.qualifier + "::" + fd.name + "' would have "
                                  "to be found before that class is complete");
            if (!peek().is("("))
                src_.fail(fd.pos, "a friend declaration declares a function, "
                                  "and '" + fd.name + "' is not one - a friend "
                                  "gets access, and only something that runs "
                                  "can use it");
            std::vector<const Type *> fparams;
            bool fvariadic = false;
            parameterTypes(fparams, fvariadic);
            if (peek().is("const"))
                src_.fail(peek().pos, "'const' here would say the function has "
                                      "a 'this' to leave alone, and a friend "
                                      "is not a member function");
            const bool friendHasBody = peek().is("{");
            const std::size_t fsigAt = functions_.size();
            declareFunction(fd.name, fd.type, fparams, fvariadic, false, fd.pos,
                            false);
            // **A friend defined here is held and replayed as a free
            // function.** [class.friend]/6 makes such a definition implicitly
            // inline, which is what a replay produces anyway.
            if (friendHasBody) {
                PendingBody held{ tag, friendStart, std::string(), fd.name,
                                  signatureAddedUnder(fd.name, fsigAt) };
                held.freeFunction = true;
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
        const Type *base = specifiers(&msc, &mquals);

        // **A typedef inside a class names a type and declares no member.** It is
        // keyed "S::value", the same qualified key a nested class uses, so it is
        // found from inside the class, from a member's body, and from outside.
        if (msc == StorageTypedef) {
            if (tag.empty())
                src_.fail(peek().pos, "a typedef needs a class with a name - "
                                      "this one is anonymous");
            do {
                Declared td = declarator(base);
                typedefFunctionSuffix(td);
                if (td.name.empty())
                    src_.fail(td.pos, "this typedef names nothing");
                declareTypeName(tag + "::" + td.name, td.type);
            } while (consume(","));
            expect(";");
            continue;
        }

        if (msc != StorageNone && msc != StorageStatic)
            src_.fail(peek().pos, "'static' is the only storage class a member "
                                  "may have");

        // **`struct Inner { ... };` declares a type and no member.**
        if (peek().is(";")) {
            // An `enum Kind { ... };` in a class body declares a type, no member.
            if (wasEnum) { at_++; continue; }
            // **An anonymous union's members are members of the class around
            // it** - [class.union] - so they take its storage and are named
            // without going through it.
            if (base->kind() == Kind::Union && base->tag().empty())
                src_.fail(peek().pos, "an anonymous union is not supported "
                                      "yet: its members would have to become "
                                      "members of the class around it, "
                                      "sharing storage - give the union a "
                                      "name and reach them through it");
            if (!base->isStructOrUnion() || base->tag().empty())
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

            // A reference member has to be bound when the object is made, so it is
            // refused by name rather than laid out as a pointer. **Only where it is a
            // member at all**: `int &get()` is a function and d.type is its return.
            const bool memberIsFunction = peek().is("(") || d.paramsAt != 0 ||
                                          d.type->isFunction();

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

            // **A reference member is bound, never assigned**, so a class with one
            // and no constructor could never be built. Said here rather than at the
            // first use, where the reader hears only that something is uninitialised.
            if (!memberIsFunction && d.type->isReference() &&
                overloadsOf(constructorKey(tag)) == nullptr && !peek().is(";"))
                src_.fail(d.pos, "'" + d.name + "' is a reference member, and a "
                                 "reference is bound where it is made - so this "
                                 "class needs a constructor with '" + d.name +
                                 "' in its initialiser list");

            // **A static member is not part of the object**, so it leaves the
            // layout untouched and the cursor where it was.
            if (msc == StorageStatic && !peek().is("(")) {
                declareStaticMember(tag, type, d, access, mquals.isVolatile);
                if (!consume(",")) break;
                continue;
            }

            if (peek().is(":")) {
                std::size_t cpos = peek().pos;
                at_++;
                long long w = constantExpression("a bit-field width");
                if (!d.type->isInteger())
                    src_.fail(d.pos, "a bit-field must have an integer type, not '" +
                                     d.type->describe() + "'");
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
                    d.name == "operatornew" || d.name == "operatordelete";
                std::vector<const Type *> mparams;
                bool mvariadic = false;
                parameterTypes(mparams, mvariadic);
                d.type = types_.functionType(d.type, std::move(mparams), mvariadic);
                // **A ref-qualifier picks the overload by the object's own
                // value category** - `f() &` against `f() &&` - which is a
                // rank the implicit object parameter does not carry here.
                if (peek().is("&") || peek().is("&&"))
                    src_.fail(peek().pos, "a ref-qualifier on a member "
                                          "function - 'f() &' or 'f() &&' - is "
                                          "not supported yet: the object's "
                                          "value category does not choose an "
                                          "overload here");
                bool constThis = false;
                if (consume("const")) constThis = true;
                // `f() volatile` and `f() const volatile`: the cv on `this` is
                // half of a member function's identity on both ABIs, and this
                // one is not in the type system - so it is refused, not read.
                if (peek().is("volatile"))
                    src_.fail(peek().pos,
                              "a 'volatile' member function is not supported yet "
                              "- the qualifier on 'this' is part of the name on "
                              "both ABIs, and 'volatile' is not in this "
                              "compiler's type system");
                // **`override` and `final` are C++11 and are checks**, not
                // declarations: the first says this must be replacing a base's
                // virtual and the second that nothing may replace it.
                if (peek().is("override") || peek().is("final"))
                    src_.fail(peek().pos, std::string("'") + peek().text +
                                  "' is not supported yet: an override is "
                                  "found by its base's slot here whether or "
                                  "not the word is written, so this would be "
                                  "a check rather than a change");
                // [class.static]/1: a static member function has no `this`, so
                // there is nothing for either of these to qualify.
                if (memberIsStatic && constThis)
                    src_.fail(d.pos, "'" + d.name + "' is a static member "
                                     "function and has no 'this', so 'const' "
                                     "has nothing to qualify");
                if (memberIsStatic && isVirtual)
                    src_.fail(d.pos, "'" + d.name + "' cannot be both 'static' "
                                     "and 'virtual' - one says there is no "
                                     "object and the other dispatches on one");
                pendingNoexcept_ = exceptionSpecification();
                // **A `constexpr` member function is implicitly const in C++11.**
                if (mquals.isConstexpr) constThis = true;

                // **`= 0` is the pure-specifier**, and not an initialiser.
                refuseDefaultedOrDeleted();

                bool isPure = false;
                if (peek().is("=") && peekAt(1).kind == TokenKind::Num &&
                    !peekAt(1).isFloat && peekAt(1).value == 0) {
                    if (!isVirtual)
                        src_.fail(peek().pos, "'= 0' makes a function pure, and "
                                              "only a virtual one can be - '" +
                                              d.name + "' is not declared "
                                              "'virtual'");
                    if (memberIsStatic)
                        src_.fail(peek().pos, "'" + d.name + "' cannot be both "
                                              "'static' and pure");
                    at_ += 2;
                    isPure = true;
                }

                // **The body is held, not parsed.** It has to be able to see members
                // declared after it, so nothing in it can be read until the class is
                // closed - which is why this is a delayed parse and not a recursion.
                if (peek().is("{")) {
                    if (tag.empty())
                        src_.fail(d.pos, "a member function needs a class with "
                                         "a name - this one is anonymous");
                    const std::size_t sigAt = functions_.size();
                    declareMember(tag, d, constThis, access,
                                  kind == Kind::Union, isVirtual, memberIsStatic,
                                  isPure);
                    pendingBodies_.push_back(PendingBody{
                        tag, itemStart, local, tag + "::" + d.name,
                        signatureAddedUnder(tag + "::" + d.name, sigAt) });
                    skipBracedBlock();
                    heldBody = true;
                    break;
                }
                if (tag.empty())
                    src_.fail(d.pos, "a member function needs a class with a "
                                     "name - this one is anonymous");
                declareMember(tag, d, constThis, access, kind == Kind::Union,
                              isVirtual, memberIsStatic, isPure);
                if (!consume(",")) break;
                continue;
            }

            if (isVirtual)
                src_.fail(d.pos, "'virtual' describes a function, and '" +
                                 d.name + "' is a data member");

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
            if (peek().is("=")) {
                at_++;
                if (peek().is("{"))
                    src_.fail(peek().pos, "a braced member initialiser is not "
                                          "supported yet - write the value");
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
            if (written[bi].isVirtual) continue;
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

    // **The virtual bases, one each, after all the non-virtual data**, and the set is transitive:
    // `Dia : D1, D2` writes no `virtual` itself yet is the class that has to lay V down, because
    // "most derived" is about the object being built and not about who wrote the keyword.
    struct Gather {
        static void of(const Type *t, std::vector<const Type *> &out,
                       std::vector<Access> &how, Access through) {
            const std::vector<Type::BaseSpec> &bs = t->bases();
            for (std::size_t i = 0; i < bs.size(); i++) {
                Access a = (bs[i].access == Access::Private ||
                            through == Access::Private) ? Access::Private
                         : (bs[i].access == Access::Protected ||
                            through == Access::Protected) ? Access::Protected
                                                          : Access::Public;
                if (bs[i].isVirtual) {
                    bool seen = false;
                    for (std::size_t k = 0; k < out.size(); k++)
                        if (out[k] == bs[i].type) seen = true;
                    if (!seen) { out.push_back(bs[i].type); how.push_back(a); }
                }
                of(bs[i].type, out, how, a);
            }
        }
    };
    std::vector<const Type *> vbases;
    std::vector<Access> vaccess;
    // The ones this class *wrote* `virtual` for. The gather below adds those
    // it only inherits, which are laid down here all the same but are not
    // direct bases of it - see BaseSpec::direct.
    std::size_t vbasesWritten = 0;
    for (std::size_t bi = 0; bi < written.size(); bi++)
        if (written[bi].isVirtual) {
            bool seen = false;
            for (std::size_t k = 0; k < vbases.size(); k++)
                if (vbases[k] == written[bi].type) seen = true;
            if (!seen) { vbases.push_back(written[bi].type);
                         vaccess.push_back(written[bi].access); }
        }
    vbasesWritten = vbases.size();
    for (std::size_t bi = 0; bi < written.size(); bi++)
        Gather::of(written[bi].type, vbases, vaccess, written[bi].access);

    for (std::size_t bi = 0; bi < vbases.size(); bi++) {
        const Type *b = vbases[bi];
        const long long byteCursor = (totalBits + 7) / 8;
        const int balign = Packed::to(b->align(target_), pack);
        const int at = static_cast<int>(alignTo(static_cast<int>(byteCursor), balign));
        const std::vector<Member> &inherited = b->members();
        for (std::size_t i = 0; i < inherited.size(); i++) {
            Member m = inherited[i];
            m.offset += at;
            if (m.declaredIn == nullptr) m.declaredIn = b;
            if (m.access == Access::Private) m.access = Access::Private;
            else if (vaccess[bi] == Access::Private) m.access = Access::Private;
            else if (vaccess[bi] == Access::Protected) m.access = Access::Protected;
            if (m.inVirtualBase == nullptr) m.inVirtualBase = b;
            members.push_back(m);
        }
        type->addBase(b, at, vaccess[bi], true, bi < vbasesWritten);
        totalBits = static_cast<long long>(at + b->dataSize()) * 8;
        if (balign > widest) widest = balign;
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
    // Held bodies are read now, with the class complete: every member exists,
    // so a body may name one declared below it. Taken out of the vector first,
    // because a body may itself define a class with held bodies of its own.
    std::vector<PendingBody> mine;
    for (std::size_t i = 0; i < pendingBodies_.size(); i++)
        if (pendingBodies_[i].tag == tag) mine.push_back(pendingBodies_[i]);
    if (!mine.empty()) {
        std::vector<PendingBody> rest;
        for (std::size_t i = 0; i < pendingBodies_.size(); i++)
            if (pendingBodies_[i].tag != tag) rest.push_back(pendingBodies_[i]);
        pendingBodies_.swap(rest);
    }

    declareImplicitSpecials(tag, type, pos);
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
    }
    // A class with a virtual base needs a table though it has no virtual
    // function: the table is where the offset to that base is kept.
    if (target_.microsoftNames()) {
        if (type->polymorphic()) emitVtable(type, tag, pos);
        emitVbtable(type, tag, pos);
    } else if (type->hasVptr()) {
        emitVtable(type, tag, pos);
    }
    // **A specialization's member bodies are not replayed here.** This is in the
    // middle of whatever asked for the class, and a replay goes through topLevel,
    // which clears the locals; they are handed to the pass that defines them.
    if (!specializationOf.empty() && deferSpecializationBodies_)
        heldForSpecialization_ = std::move(mine);
    else
        replayInlineBodies(std::move(mine));
    return type;
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
    if ([this] { std::size_t save = at_; bool t = atTypeName(); at_ = save; return t; }()) {
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

    // **A scoped enumeration is C++11 and is a type of its own** - its
    // enumerators do not leak into the enclosing scope and do not convert to
    // int.
    if (peek().is("class") || peek().is("struct"))
        src_.fail(peek().pos, "a scoped enumeration - 'enum class' - is not "
                              "supported yet: an enumeration is an int that "
                              "remembers its name here, where a scoped one is "
                              "a distinct type whose enumerators are reached "
                              "through it");

    std::string tag;
    if (peek().kind == TokenKind::Ident) { tag = peek().text; at_++; }

    // **An enum-base fixes the underlying type** - `enum E : unsigned char` -
    // and with it the enumeration's size, signedness and the type of each
    // enumerator; [dcl.enum]/5 wants an integral type that is not bool.
    const Type *underlying = nullptr;
    if (consume(":")) {
        std::size_t upos = peek().pos;
        StorageClass usc;
        underlying = specifiers(&usc)->unqualified();
        if (!underlying->isInteger() || underlying->kind() == Kind::Bool)
            src_.fail(upos, "an enum-base must be an integral type other than "
                            "bool, and '" + underlying->describe() + "' is not");
    }

    // **An enum is named through what encloses it**, the same way a class is:
    // `C::Kind` inside a class and `n::Kind` inside a namespace.
    const Type *within = classStack_.empty() ? nullptr : classStack_.back();
    std::string prefix;
    if (within != nullptr) prefix = within->tag() + "::";
    else if (!namespaceStack_.empty()) prefix = namespacePrefix();

    // The tag names a type, as a class tag does. What it does not yet name is
    // a *distinct* type: an enumeration is still its integer here, so the
    // conversions C++ refuses in both directions are accepted (CONFORMANCE.md).
    const Type *self = nullptr;
    if (!tag.empty()) {
        self = types_.enumType(prefix + tag,
                               underlying != nullptr ? underlying->kind() : Kind::Int);
        declareTypeName(prefix + tag, self);
    }
    // What a based enumeration's values are: its own type, so that `sizeof(A)`
    // and the promotions come out as the base says. An int enum stays int.
    const Type *valueType = underlying == nullptr ? nullptr
                          : self != nullptr ? self : underlying;
    const Type *narrowAs = underlying != nullptr ? underlying : types_.intType();

    if (!peek().is("{")) return valueType != nullptr ? valueType : types_.intType();
    at_++;

    long long next = 0;
    while (!peek().is("}")) {
        std::size_t npos = peek().pos;
        std::string name = expectIdent("an enumerator");
        if (findEnum(prefix + name))
            src_.fail(npos, "'" + name + "' is declared twice");
        if (consume("="))
            next = narrowTo(constantExpression("a constant"), narrowAs);
        enumIndex_[prefix + name] = enums_.size();
        enums_.push_back(EnumConst{ prefix + name, next, valueType });
        next = next + 1;
        if (!consume(",")) break;
    }
    expect("}");
    if (enums_.empty()) src_.fail(pos, "enum has no enumerators");
    return valueType != nullptr ? valueType : types_.intType();
}

// The specifiers are read without their qualifiers here, and specifiers() folds the
// const in afterwards. It reads 'const' in two places - before the type name and
// after it - and both must be collected before the type can be built.
const Type *Parser::specifiers(StorageClass *storage, Qualifiers *quals) {
    Qualifiers discard;
    if (quals == nullptr) quals = &discard;
    const Type *t = unqualifiedSpecifiers(storage, quals);
    if (quals->isVolatile) refuseVolatileUnderADeclarator(*storage);
    return quals->isConst ? types_.withConst(t) : t;
}

// Marks the file as one the optimizer must leave alone; always true.
bool Parser::sawVolatile() {
    if (current_ != nullptr) current_->usesVolatile = true;
    return true;
}

// **`volatile` is read and dropped, and this is the line where that stops
// being honest.** There is no volatile in this type system; on an object that
// costs nothing only while the file is not optimised - see Program::usesVolatile.
void Parser::refuseVolatilePointer() {
    src_.fail(peek().pos,
              "a 'volatile' pointer - 'T *volatile' - is not supported yet: cl "
              "writes 'REAH' where a plain pointer is 'PEAH', and 'volatile' is "
              "not in this compiler's type system");
}

// **A variable's name carries its cv on the Microsoft ABI and not on Itanium**, measured: `volatile
// int g;` is `?g@@3HC` on cl where a plain int is `?g@@3HA`, and `_Z`-nothing on both Itanium
// targets, which do not decorate a variable at all.
void Parser::refuseVolatileWithLinkage(bool written, bool internal,
                                       std::size_t pos) {
    if (!written || internal || !target_.microsoftNames()) return;
    src_.fail(pos, "a 'volatile' object with external linkage is not supported "
                   "yet for x86_64-windows: cl decorates its name with the "
                   "qualifier - '?g@@3HC' where a plain int is '?g@@3HA' - and "
                   "'volatile' is not in this compiler's type system. The two "
                   "Itanium targets do not decorate a variable's name, so it is "
                   "accepted there");
}

void Parser::refuseVolatileUnderADeclarator(StorageClass storage) {
    // `volatile int (*p)[3]` hides its star behind a parenthesis.
    const bool pointerNext =
        peek().is("*") || peek().is("&") || peek().is("&&") ||
        (peek().is("(") && (peekAt(1).is("*") || peekAt(1).is("&") ||
                            peekAt(1).is("&&")));
    if (pointerNext)
        src_.fail(peek().pos,
                  "a pointer or reference to a 'volatile' type is not supported "
                  "yet - the qualifier is not in this compiler's type system, so "
                  "this would be named 'int *' where clang writes 'PVi' and cl "
                  "writes 'PECH'. A 'volatile' object of its own is read and "
                  "written here as it should be, and is not refused");
    if (storage == StorageTypedef)
        src_.fail(peek().pos,
                  "a typedef of a 'volatile' type is not supported yet - the "
                  "qualifier is not in this compiler's type system, and a name "
                  "for it would carry it past the refusal a written "
                  "'volatile T *' meets");
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

    for (;;) {
        if (peek().is("alignas")) {
            int a = alignasSpecifier();
            if (a > quals->alignAs) quals->alignAs = a;
            continue;
        }
        if (consume("static"))  { *storage = StorageStatic; continue; }
        // **`extern template` suppresses an implicit instantiation** in this
        // translation unit and promises one elsewhere. Every specialization
        // here is emitted where it is used, so the promise cannot be kept.
        if (peek().is("extern") && peekAt(1).is("template"))
            src_.fail(peek().pos, "an explicit instantiation declaration - "
                                  "'extern template' - is not supported yet: "
                                  "a specialization is emitted wherever it is "
                                  "used here, so there is nothing to suppress");
        if (consume("extern"))  { *storage = StorageExtern; continue; }
        if (consume("typedef")) { *storage = StorageTypedef; continue; }
        if (consume("const"))    { quals->isConst = true; continue; }
        // **`constexpr` on an object is `const` plus a demand.** [dcl.constexpr]
        // makes the object const and the rest of the compiler wants to know nothing
        // else, which is why almost nothing downstream mentions constexpr at all.
        if (consume("constexpr")) {
            quals->isConst = true;
            quals->isConstexpr = true;
            continue;
        }
        if (consume("volatile")) { quals->isVolatile = sawVolatile(); continue; }
        // **`inline` is a hint about linkage, not about the type.** It makes a
        // function's definition mergeable across translation units; nothing
        // else downstream needs it.
        if (peek().is("inline") && peekAt(1).is("namespace"))
            src_.fail(peek().pos, "an inline namespace is not supported yet - "
                                  "its members would have to be found in the "
                                  "namespace around it, and 'namespace N { }' "
                                  "without 'inline' works here");
        if (consume("inline")) { quals->isInline = true; continue; }
        if (consume("register")) { *storage = StorageRegister; continue; }
        if (consume("auto"))     { *storage = StorageAuto; continue; }
        break;
    }

    // wchar_t is a type of its own in C++, not the typedef C makes it.
    if (consume("wchar_t")) return types_.get(target_.wcharType());
    // **`Point::Point(...)` has no type before the name, and the name is a type.**
    if (atUntypedMemberDefinition()) return types_.get(Kind::Void);

    // Replaying an inline constructor or destructor: the tokens are `X(` or
    // `~X(` with no type in front, exactly as they were written in the class.
    if (!inlineOwner_.empty() &&
        ((peek().kind == TokenKind::Ident && peek().text == inlineOwnerName_ &&
          peekAt(1).is("(")) ||
         (peek().is("~") && peekAt(1).kind == TokenKind::Ident &&
          peekAt(1).text == inlineOwnerName_)))
        return types_.get(Kind::Void);

    // **`typename` is a hint this compiler does not need, so it is read and dropped.**
    // It tells a parser that a dependent qualified name is a type, which matters only
    // where a body is parsed before its arguments; this one replays at instantiation.
    if (consume("typename")) {
        if (peek().kind != TokenKind::Ident)
            src_.fail(peek().pos, "'typename' introduces a qualified type "
                                  "name, and this is not one");
    }

    // A class template with its arguments *is* a type. A function template
    // named where a type was expected is not, and is refused by name.
    if (peek().kind == TokenKind::Ident && isTemplateName(peek().text) &&
        peekAt(1).is("<")) {
        const TemplateDecl decl = findTemplate(peek().text)->second;
        if (!decl.isClass) refuseTemplateId();
        const std::size_t tpos = peek().pos;
        at_++;
        const Type *cls = instantiateClass(decl, tpos);

        return memberTypeWalk(cls);
    }

    if (peek().is("decltype")) return decltypeSpecifier();

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
            std::size_t consumed = 0;
            // **`std::vector<int>` - a class template named qualified.**
            std::string tmpl;
            std::size_t tmplConsumed = 0;
            for (std::size_t k = 1; peekAt(k).is("::") &&
                                    peekAt(k + 1).kind == TokenKind::Ident;
                 k += 2) {
                q += "::" + peekAt(k + 1).text;
                if (const Type *n = findTypedef(q)) {
                    found = n;
                    consumed = k + 2;
                }
                if (peekAt(k + 2).is("<") && isClassTemplate(q)) {
                    tmpl = q;
                    tmplConsumed = k + 2;
                }
            }
            if (tmplConsumed > consumed) {
                const std::size_t tpos = peek().pos;
                at_ += tmplConsumed;
                const Type *cls = instantiateClass(findTemplate(tmpl)->second,
                                                   tpos);
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
                at_ += consumed;
                return found;
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
        if (consume("const"))         { quals->isConst = true; continue; }
        if (consume("constexpr")) {
            quals->isConst = true;
            quals->isConstexpr = true;
            continue;
        }
        if (consume("volatile"))      { quals->isVolatile = sawVolatile(); continue; }
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
    if (*storage == StorageAuto) {
        *storage = StorageNone;
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
    // **`[[`, which is an attribute and not a type.** Named here because the C++11
    // attributes and the C++14 one are spelled identically, and a reader who writes
    // either is owed the version number rather than a complaint about a missing type.
    if (peek().is("[") && peekAt(1).is("["))
        src_.fail(peek().pos, "an attribute is not supported yet - C++11 has "
                              "'[[noreturn]]' and '[[carries_dependency]]', "
                              "and '[[deprecated]]' is C++14");
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
        if (overloadsOf(constructorKey(u->tag())) != nullptr) return false;
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
    while (consume("[")) {
        if (consume("]")) { dims.push_back(-1); continue; }
        std::size_t dpos = peek().pos;
        // `long long`, not `long`: this compiler is built by cl on one of its three
        // machines, where a `long` is 32 bits - so `char a[0x100000001]` silently
        // became `char a[1]` there and kept its full length on the other two.
        long long n = constantExpression("an array length");
        if (n <= 0)
            src_.fail(dpos, "an array length must be positive, not " +
                            std::to_string(n));
        dims.push_back(n);
        expect("]");
    }
    for (std::size_t i = 1; i < dims.size(); i++)
        if (dims[i] < 0)
            src_.fail(pos, "only the first dimension may be left empty - the "
                           "others decide how far one step moves");

    // **An object this compiler cannot measure is refused where it is written.** Every
    // size here is a signed 32-bit count, and an array that overflowed one used to be
    // laid out anyway. Checked by division, so the check itself cannot overflow.
    for (std::size_t i = dims.size(); i-- > 0; ) {
        const long long elem = base->size(target_);
        if (dims[i] > 0 && elem > 0 && dims[i] > 2147483647LL / elem)
            src_.fail(pos, "this array is larger than this compiler can lay "
                           "out: " + std::to_string(dims[i]) + " elements of " +
                           std::to_string(elem) + " bytes is past the "
                           "2147483647 an object here is measured in");
        base = types_.arrayOf(base, dims[i]);
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
    const std::size_t pos = peek().pos;
    at_++;                                    // `operator`

    // The two that are written as a *pair* of tokens. `operator()` has to be
    // read here rather than left to the parameter list below, which would
    // take the `()` for an empty one and leave the declaration with no name.
    if (peek().is("(")) { at_++; expect(")"); return "operator()"; }
    if (peek().is("[")) { at_++; expect("]"); return "operator[]"; }

    const std::string spelling = peek().text;

    // **The array forms are refused by name**: `new T[n]` calls the
    // platform's `operator new[]` and a class's plain `operator new` is not
    // consulted for it, so a declared one could not be reached.
    if ((spelling == "new" || spelling == "delete") && peekAt(1).is("["))
        src_.fail(pos, "'operator " + spelling + "[]' is not supported yet - "
                       "the array forms are the platform's; 'operator " +
                       spelling + "' can be declared, replaced and given to a class");
    if (spelling == "->*")
        src_.fail(pos, "'operator->*' is not supported yet");
    if (peek().kind == TokenKind::Str)
        src_.fail(pos, "a user-defined literal is not supported yet");
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
                    if (peek().is("volatile")) refuseVolatilePointer();
                    to = types_.withConst(to);
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
                                     bool member, std::size_t pos) {
    const std::string spelling = operatorSpelling(name);
    if (spelling.empty() || findOperator(spelling) == nullptr) return;
    // **An allocation function is an operator in name only** - [basic.stc.dynamic]:
    // `operator new` takes a size_t and `operator delete` a `void *`, member or
    // not, and nothing below applies to them.
    if (spelling == "new" || spelling == "delete") {
        const bool sized = spelling == "new";
        const Type *first = params.empty() ? nullptr : params[0]->unqualified();
        const bool ok = first != nullptr &&
                        (sized ? first == types_.get(target_.sizeType())
                               : (first->isPointer() && first->pointee()->unqualified()->isVoid()));
        if (!ok)
            src_.fail(pos, "'" + name + "' must take " +
                           std::string(sized ? "a size_t" : "a 'void *'") +
                           " first - [basic.stc.dynamic.allocation]");
        // The library's placement pair - `(size_t, void *)`, `(void *, void
        // *)` - may be declared, as <new> declares it; `new (p) T` builds at
        // p without calling it, which is what that pair does.
        if (params.size() == 2 && !member) {
            const Type *second = params[1]->unqualified();
            if (second->isPointer() && second->pointee()->unqualified()->isVoid()) return;
        }
        if (params.size() != 1)
            src_.fail(pos, "'" + name + "' with " + std::to_string(params.size()) +
                           " parameters is not supported yet - the one-parameter "
                           "form is what a new-expression and a delete-expression "
                           "call here, and the library's placement form may be "
                           "declared");
        return;
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

    static const char *const binary[] = {
        "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
        "==", "!=", "<", "<=", ">", ">="
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

// `expectIdent` with the operator case in front of it. This stands wherever a
// declarator reads a name, which is three places: the plain one, the one
// after a `::`, and the one after a class template's argument list.
std::string Parser::declaredName(const char *what) {
    if (peek().is("operator")) return operatorName();
    return expectIdent(what);
}

Parser::Declared Parser::declarator(const Type *base, bool nameOptional,
                                    bool insideParens) {

    // The const after a star qualifies the pointer, not what it points at: `char *
    // const p` is a const pointer to a writable char and `const char *p` the other
    // way round. Both are differences of type, so the declarator remembers neither.
    while (consume("*")) {
        base = types_.pointerTo(base);
        for (;;) {
            if (consume("const"))    { base = types_.withConst(base); continue; }
            if (peek().is("volatile")) refuseVolatilePointer();
            break;
        }
    }

    // A reference binds after every star - `int *&r` is a reference to a pointer -
    // and there is nothing on the other side of it, a reference being no object to
    // point at. **`&&` binds like `&`**, differing only in what it will take.
    if (consume("&&")) {
        base = types_.rvalueReferenceTo(base);
        if (peek().is("&") || peek().is("&&"))
            src_.fail(peek().pos, "there is no reference to a reference");
        if (peek().is("*"))
            src_.fail(peek().pos, "there is no pointer to a reference");
        return declarator(base, nameOptional, insideParens);
    }
    if (consume("&")) {
        base = types_.referenceTo(base);
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

    if (peek().is("(")) {
        std::size_t open = at_;
        at_++;
        // `int (*p)()` and `int (S::*p)()` are the same shape to this branch: what is
        // inside the parentheses points at something, so what follows them is a
        // parameter list and not an array bound - or the inner base is wrong.
        const bool wrapsMemberPointer = peek().kind == TokenKind::Ident &&
                                        peekAt(1).is("::") && peekAt(2).is("*");
        bool wrapsAPointer = peek().is("*") || wrapsMemberPointer;

        declarator(types_.intType(), true, true);
        expect(")");

        std::size_t posOuter = peek().pos;
        const Type *outer;
        if (peek().is("(") && wrapsAPointer) {
            std::vector<const Type *> params;
            bool variadic = false;
            parameterTypes(params, variadic);
            // **`int (S::*f)() const` is a different type and is refused by name.** A
            // function type here carries no constness, so taking the word would make
            // a pointer that could hold a non-const member and be called on a const.
            if (wrapsMemberPointer && peek().is("const"))
                src_.fail(peek().pos, "a pointer to a *const* member function "
                                      "is not supported yet - the constness of "
                                      "'this' is not part of a function type "
                                      "here, so this one cannot be told from "
                                      "the other");
            outer = types_.functionType(base, std::move(params), variadic);
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
            if (!peek().is("::"))
                src_.fail(peek().pos, "'" + cls->tag() + "' is a type here, "
                                      "and a declaration needs a name after "
                                      "it");
            at_++;
            qualifier = cls->tag();
            name = declaredName("a member name");
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
            const Type *cls = findTypedef(of);
            if (cls == nullptr || !cls->isStructOrUnion())
                src_.fail(pos, "'" + of + "' is not a class, so '" + of +
                               "::*' names no member of anything");
            at_ += 2;                              // the '::' and the '*'
            // cl sizes a member pointer by the class's inheritance model, and an
            // incomplete class takes the three-field "unspecified" form: refused.
            if (target_.microsoftNames() && !cls->isComplete())
                src_.fail(pos, "'" + of + "::*' before '" + of + "' is complete - "
                               "the Microsoft ABI sizes a pointer to a member by "
                               "the class's inheritance model, and an incomplete "
                               "class takes the widest form (cl's unspecified "
                               "model, 12 and 24 bytes). Not supported yet; "
                               "define the class first");
            const Type *mp;
            if (base->isFunction()) {
                mp = types_.memberFunctionPointerTo(cls, base, target_);
            } else {
                mp = types_.memberPointerTo(cls, base);
            }
            for (;;) {
                if (consume("const"))    { mp = types_.withConst(mp); continue; }
                if (peek().is("volatile")) refuseVolatilePointer();
                break;
            }
            return declarator(mp, nameOptional, insideParens);
        }
        at_++;
        qualifier = qualifier.empty() ? name : qualifier + "::" + name;
        bool destructor = consume("~");
        name = declaredName("a member name after '::'");
        if (destructor) name = "~" + name;
    }

    const Type *t = arraySuffix(base, pos);

    std::size_t paramsAt = 0;
    if (insideParens && peek().is("(")) {
        paramsAt = at_;
        std::vector<const Type *> ignored;
        bool ignoredVariadic = false;
        parameterTypes(ignored, ignoredVariadic);
    }

    return Declared{ name, t, pos, paramsAt, qualifier };
}

