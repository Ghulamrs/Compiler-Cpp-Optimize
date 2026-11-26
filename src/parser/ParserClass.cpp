// The parser: what a class needs written for it. Constructors and destructors,
// vtables and thunks, the implicit special members and the code that defines them
// when something calls one, and the static data members. Rungs 3 and 4.
#include <functional>
#include <cstdio>
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <algorithm>
#include <climits>
#include <cstring>

StmtPtr Parser::constructLocal(const Declared &d, int offset,
                               std::vector<ExprPtr> args, bool copyInit,
                               bool valueInit) {
    return constructObject(d, std::string(), offset, std::move(args), copyInit,
                           valueInit);
}

// The same construction wherever the object lives: a frame slot, or a global
// named by `symbol` - a static local, a file-scope object, a static member.
StmtPtr Parser::constructObject(const Declared &d, const std::string &symbol,
                                int offset, std::vector<ExprPtr> args,
                                bool copyInit, bool valueInit) {
    checkNotAbstract(d.type, d.pos, "'" + d.name + "'");
    // **[class.copy]/31: built from a temporary of its own class, the object is that temporary** - the
    // temporary's constructor is given the object's address, and no copy or move constructor is called. A
    // file-scope object, a static local, an element (1.78; a local takes the temporary's slot instead). The
    // constructor that would have copied is still the one [class.copy]/31 has checked: chosen, not explicit
    // in a copy-initialisation, accessible - or the copy is made, and refused where it must be.
    if (args.size() == 1 && freshTemporarySlot(*args[0], d.type) != 0) {
        const Type *cls = d.type->unqualified();
        const Signature *sel = moveConstructorOf(cls);
        if (sel == nullptr) sel = copyConstructorOf(cls);
        const bool mayBeChosen = sel != nullptr && !sel->deleted && !(copyInit && sel->isExplicit) &&
                                 (sel->access == Access::Public || insideAccessOf(cls, sel->access) || isFriendOf(cls));
        if (mayBeChosen)
            if (Call *built = temporaryConstruction(*args[0])) {
                const int slot = freshTemporarySlot(*args[0], d.type);
                ExprPtr at(new Unary('&', objectAt(d, symbol, offset)));
                at->setType(types_.pointerTo(cls));
                built->setArg(0, std::move(at));
                adoptTemporary(slot);
                return StmtPtr(new ExprStmt(std::move(args[0])));
            }
    }
    const std::string key = constructorKey(d.type->tag());
    Signature ctor;
    {
        ConvertingOnly only(*this, copyInit);
        ctor = resolveOverload(key, args, d.pos);
    }
    applyDefaults(ctor, args, d.pos);

    // **[dcl.init]/16: copy-initialization may not pick an `explicit` constructor.**
    // `S s(3);` and `S s = 3;` call the same function, and this is the whole of what
    // `explicit` does. Checked after resolution, so the reader is told which one.
    if (copyInit && ctor.isExplicit)
        src_.fail(d.pos, "'" + d.type->describe() + "' has a constructor "
                         "taking these arguments and it is 'explicit', so it "
                         "will not be chosen for '" + d.name + " = ...' - "
                         "write '" + d.type->describe() + " " + d.name +
                         "(...)', which asks for it by name");

    if (ctor.access != Access::Public &&
        !insideAccessOf(d.type, ctor.access) && !isFriendOf(d.type))
        src_.fail(d.pos, "'" + d.type->describe() + "' has no public constructor "
                         "taking these arguments - the one that matches is " +
                         (ctor.access == Access::Private ? "private" : "protected"));

    const Type *thisType = types_.pointerTo(d.type->unqualified());
    ExprPtr addr(new Unary('&', objectAt(d, symbol, offset)));
    addr->setType(thisType);

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));

    std::vector<const Type *> full;
    full.push_back(thisType);
    for (std::size_t i = 0; i < ctor.params.size(); i++) full.push_back(ctor.params[i]);

    ExprPtr call = completeCall(d.type->tag(), ctor.symbol, nullptr,
                                types_.get(Kind::Void), full, false, d.pos,
                                std::move(all));

    // **The zeroing half of [dcl.init]/8, spelt as the temporary path spells
    // it.** A constructor somebody wrote is the whole of the initialisation; an
    // implicit one leaves the members it does not name, so `{}` zeroes first.
    if (valueInit && ctor.implicit) {
        ExprPtr fresh = objectAt(d, symbol, offset);
        if (ExprPtr chain = zeroChain(*fresh, d.type->unqualified())) {
            ExprPtr seq(new Comma(std::move(chain), std::move(call)));
            seq->setType(types_.get(Kind::Void));
            call = std::move(seq);
        }
    }
    return StmtPtr(new ExprStmt(std::move(call)));
}

// The deleting destructor's name. Built through the manglers rather than by
// concatenation, because a nested class's is a whole nested-name -
// ??_GInner@Outer@@UEAAPEAXI@Z, not ??_GOuter::Inner@@...
bool Parser::overrides(const VSlot &s, const std::string &name,
                       const std::vector<const Type *> &params, bool constThis,
                       RefQual refThis, bool volatileThis) {
    if (s.name != name || s.constThis != constThis || s.refThis != refThis ||
        s.volatileThis != volatileThis) return false;
    return sameParameters(s.params, params);
}

// [over.load]/2: with one parameter-type-list, either every overload has a ref-qualifier or none has.
static bool mixesRefQualifier(RefQual a, RefQual b) {
    return (a == RefQual::None) != (b == RefQual::None);
}

std::string Parser::refQualifierMixRefusal(RefQual written, RefQual other) {
    const RefQual with = written != RefQual::None ? written : other;
    const std::string spelt = std::string("with ref-qualifier '") +
                              (with == RefQual::LValue ? "&" : "&&") + "'";
    const std::string none = "without a ref-qualifier";
    return "cannot overload a member function " + (written != RefQual::None ? spelt : none) +
           " with a member function " + (written != RefQual::None ? none : spelt) + " - [over.load]/2";
}

// **[class.virtual]/8: an override returns the same type, or a pointer or
// reference to a class derived from the one the base's returns.** The slot was
// taken on name and parameters alone: `double f()` over `virtual int f()` ran.
void Parser::checkOverrideReturn(const VSlot &s, const Type *returns, const std::string &cls,
                                 const std::string &name, std::size_t pos) const {
    if (s.returns == nullptr || returns == nullptr) return;
    if (s.returns == returns || s.returns->describe() == returns->describe()) return;
    const bool ptr = s.returns->isPointer() && returns->isPointer();
    const bool ref = s.returns->isReference() && returns->isReference();
    if (ptr || ref) {
        const Type *base = s.returns->pointee(), *over = returns->pointee();
        if (base->unqualified()->isStructOrUnion() && over->unqualified()->isStructOrUnion() &&
            (base->isConst() || !over->isConst())) {
            const int off = publicBaseOffset(over, base);
            if (off == 0) return;
            if (off > 0)
                src_.fail(pos, "'" + cls + "::" + name + "' returns '" + returns->describe() +
                               "' over the base's '" + s.returns->describe() + "' - a covariant "
                               "return, and this one moves the result by " + std::to_string(off) +
                               " bytes at every call through the base, which needs a thunk this "
                               "compiler does not build yet. Return the base's type here");
        }
    }
    src_.fail(pos, "'" + cls + "::" + name + "' overrides a virtual function with a "
                   "different return type: '" + returns->describe() + "' where the base's "
                   "is '" + s.returns->describe() + "'. An override returns the same type, "
                   "or a pointer or reference to a class derived from what the base's "
                   "points at");
}

std::string Parser::deletingDestructorSymbol(const std::string &cls) {
    return target_.microsoftNames()
         ? microsoftDeletingDestructorName(cls, findTypedef(cls))
         : itaniumDeletingDestructorName(cls, findTypedef(cls));
}

// [class.ctor]/1 and [class.dtor]/1 give neither a cv-qualifier nor a ref-qualifier, as clang says.
void Parser::refuseSpecialRefQualifier(const char *what) const {
    if (!peek().is("&") && !peek().is("&&")) return;
    src_.fail(peek().pos, "ref-qualifier '" + peek().text + "' is not allowed on a " + what);
}

void Parser::declareDestructor(const std::string &cls, std::size_t pos,
                               Access access, bool isVirtual) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    if (!params.empty() || variadic)
        src_.fail(pos, "a destructor takes no parameters");
    refuseSpecialRefQualifier("destructor");
    // A destructor is `noexcept` in C++11 whether or not it says so.
    pendingNoexcept_ = exceptionSpecification();
    const VirtSpecifiers virt = takeVirtSpecifiers();
    const SpecialDef special = takeSpecialDef();

    if (overloadsOf(destructorKey(cls)) != nullptr)
        src_.fail(pos, "'" + cls + "' has two destructors, and a class has one");
    // [dcl.fct.def.delete]/2: every death of an object of the class is a use of it, and each goes
    // through destructorCall, which refuses a deleted one where it is asked for.
    if (special == SpecialDef::Deleted) {
        registerDestructor(cls, pos, access, isVirtual, false, virt);
        functions_[(*overloadsOf(destructorKey(cls)))[0]].deleted = true;
        return;
    }
    // **A defaulted destructor is the implicit one.** A virtual one is registered now, so that it has its
    // slot whether or not a member gives it work; the rest is left to declareImplicitDestructor.
    if (special == SpecialDef::Defaulted) {
        defaultedMember(defaulted_[cls].dtor, access, pos, "the destructor");
        if (isVirtual || virt.isOverride || virt.isFinal)
            registerDestructor(cls, pos, access, isVirtual, true, virt);
        return;
    }
    // **`= 0`, the pure-specifier**: a pure virtual destructor makes the class abstract and is
    // defined all the same, since every derived destructor calls it ([class.dtor]/9). Its slot keeps
    // the destructor, so the table names a function that exists. Refused with "expected ';'" before.
    bool pure = false;
    if (special == SpecialDef::None && peek().is("=") && peekAt(1).kind == TokenKind::Num &&
        !peekAt(1).isFloat && peekAt(1).value == 0) {
        if (!isVirtual)
            src_.fail(peek().pos, "'~" + localOf(cls) + "' is pure, and only a virtual function can "
                                  "be - write 'virtual ~" + localOf(cls) + "() = 0;'");
        at_ += 2;
        pure = true;
    }
    registerDestructor(cls, pos, access, isVirtual, false, virt);
    if (pure)
        for (VSlot &s : vtables_[cls])
            if (s.name == "~") s.pure = true;
}

// Everything a destructor needs in the tables, whether a program wrote it or
// the compiler did: the name, the entry, and the vtable slots a virtual one
// claims.
void Parser::registerDestructor(const std::string &cls, std::size_t pos,
                                Access access, bool isVirtual, bool implicit,
                                VirtSpecifiers virt, const Type *type) {
    const std::vector<const Type *> params;
    const std::string key = destructorKey(cls);
    const std::string name = "~" + localOf(cls);

    // **A base with a virtual destructor makes this one virtual**, keyword or not -
    // [class.dtor]. The base's slots are already in this class's table, so it is
    // answered by looking for the "~" entry, and before the name is built.
    std::vector<VSlot> &slots = vtables_[cls];
    std::size_t slot = slots.size();
    bool foundBase = false;
    const Type *msDtorVbase = nullptr;    // Microsoft: the virtual base whose table holds this one (M3)
    for (std::size_t i = 0; i < slots.size(); i++)
        if (slots[i].name == "~") {
            checkNotFinal(slots[i], cls, name, pos);
            slot = i; isVirtual = foundBase = true; break;
        }
    // And a base off the primary chain - a second base, a virtual one - whose
    // destructor is virtual makes this one virtual too, in a slot of its own.
    // On Microsoft that base's table is the slot, so it is looked for whenever this class has none.
    if (!isVirtual || (target_.microsoftNames() && !foundBase))
        if (const Type *self = type != nullptr ? type : findTypedef(cls)) {
            const std::vector<Type::BaseSpec> &bs = self->bases();
            for (std::size_t bi = 0; bi < bs.size() && !foundBase; bi++) {
                std::map<std::string, std::vector<VSlot> >::const_iterator it =
                    vtables_.find(bs[bi].type->tag());
                if (it == vtables_.end()) continue;
                for (std::size_t i = 0; i < it->second.size(); i++)
                    if (it->second[i].name == "~") {
                        checkNotFinal(it->second[i], cls, name, pos);
                        if (target_.microsoftNames() && !bs[bi].isVirtual)
                            src_.fail(pos, "'" + cls + "' has a virtual destructor only in a base after its "
                                           "first polymorphic one, and the Microsoft ABI enters it at that "
                                           "base's vfptr - not supported yet for x86_64-windows");
                        if (target_.microsoftNames()) msDtorVbase = bs[bi].type->unqualified();
                        isVirtual = foundBase = true; break;
                    }
            }
        }
    // [class.virtual]/4, for the destructor: `override` wants a base's virtual one and `final` a virtual one.
    if (virt.isOverride && !foundBase)
        src_.fail(virt.overrideAt, "'" + cls + "::" + name + "' is marked 'override' but overrides "
                                   "nothing - no base has a virtual destructor");
    if (virt.isFinal && !isVirtual)
        src_.fail(virt.finalAt, "'" + cls + "::" + name + "' is marked 'final' and is not virtual "
                                "- only a virtual destructor can be");

    // **A virtual destructor is U on Microsoft whatever its access**, the same
    // rule a virtual member function already followed - measured with cl,
    // which writes ??1VB@@UEAA@XZ where a non-virtual public one is QEAA.
    const char code = isVirtual                   ? 'U'
                    : access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    std::string out;
    if (target_.microsoftNames()) out = microsoftDestructorName(cls, findTypedef(cls), code);
    else                          itaniumDestructorName(cls, findTypedef(cls), true, &out);

    functionIndex_[key].push_back(functions_.size());
    functions_.push_back(Signature{ "~" + localOf(cls), out, types_.get(Kind::Void),
                                    params, false, false, pos, false, cls, false,
                                    access, isVirtual });
    functions_.back().implicit = implicit;
    if (!implicit) {
        functions_.back().isNoexcept = pendingNoexcept_;
        keepDeferredSpec();
        functions_.back().spec = pendingSpec_;
    }

    // **A virtual destructor claims slots where it is declared**, and how many
    // depends on the ABI: Itanium two and adjacent, Microsoft one holding only the
    // deleting form. A derived class overrides in place, matching on "~".
    if (!isVirtual) return;
    const bool ms = target_.microsoftNames();
    const std::string deleting = deletingDestructorSymbol(cls);
    // Entered at that virtual base, `??1` and `??_G` both, as cl's are; no slot here.
    if (msDtorVbase != nullptr && slot == slots.size()) {
        msVbaseAdjust_[deleting] = MsVbaseAdjust{ cls, msDtorVbase, 0 };
        msVbaseAdjust_[out] = MsVbaseAdjust{ cls, msDtorVbase, 0 };
        return;
    }

    std::vector<const Type *> none;
    if (slot < slots.size()) {
        slots[slot].symbol = ms ? deleting : out;       // the complete form
        slots[slot].isFinal = virt.isFinal;
        slots[slot].pure = false;                       // a base's pure destructor, overridden here
        if (!ms && slot + 1 < slots.size() &&
            slots[slot + 1].name == "~$deleting")
            slots[slot + 1].symbol = deleting;
        return;
    }
    slots.push_back(VSlot{ "~", ms ? deleting : out, none, false });
    slots.back().isFinal = virt.isFinal;
    if (!ms) slots.push_back(VSlot{ "~$deleting", deleting, none, false });
}

// **The table a vbptr reads, by name**: the class's own where it holds one
// pointer, else the holding subobject's, with its path through the bases when
// one class holds two (`G : virtual D, F` named both of B's tables alike).
static std::string vbptrTable(const std::string &tag, const std::vector<Type::VbPtr> &ptrs,
                              std::size_t p) {
    if (ptrs.size() == 1 || ptrs[p].base == nullptr) return vbtableSymbol(tag);
    int same = 0;
    for (const Type::VbPtr &q : ptrs) if (q.base == ptrs[p].base) same++;
    if (same < 2 || ptrs[p].via == nullptr || ptrs[p].via == ptrs[p].base)
        return vbtableSymbol(tag, ptrs[p].base->tag());
    return vbtableSymbol(tag, ptrs[p].base->tag(), ptrs[p].via->tag());
}

// **The vbtable pointer, which only the Microsoft ABI has.**
void Parser::storeVbptr(const std::string &cls, const Type *memberOf,
                        int thisSlot, std::vector<StmtPtr> &into) {
    const std::vector<Type::VbPtr> &ptrs = memberOf->vbptrs();
    const Type *charPtr = types_.pointerTo(types_.get(Kind::Char));
    const Type *entry = types_.intType();

    // **One store per pointer.**
    for (std::size_t p = 0; p < ptrs.size(); p++) {
        const int vbp = ptrs[p].offset;
        const std::string symbol = vbptrTable(cls, ptrs, p);

        // The table's address, the same way the vftable's is taken: a global
        // with an array type, decayed, so what is stored is where it is and
        // not what its first word happens to hold.
        int entries = 1;
        const std::vector<Type::BaseSpec> &bs =
            p == 0 || ptrs[p].base == nullptr ? memberOf->bases()
                                              : ptrs[p].base->bases();
        for (std::size_t i = 0; i < bs.size(); i++)
            if (bs[i].isVirtual) entries++;
        ExprPtr table(Var::global(symbol));
        table->setType(types_.arrayOf(entry, entries));
        ExprPtr value(new Cast(charPtr, decay(std::move(table))));
        value->setType(charPtr);

        ExprPtr self(Var::local("this", thisSlot));
        self->setType(charPtr);
        ExprPtr where = std::move(self);
        if (vbp != 0) {
            ExprPtr at(new Num(static_cast<long long>(vbp)));
            at->setType(types_.get(Kind::LongLong));
            ExprPtr sum(new Binary(BinOp::Add, std::move(where),
                                   std::move(at)));
            sum->setType(charPtr);
            where = std::move(sum);
        }
        ExprPtr slot(new Cast(types_.pointerTo(charPtr), std::move(where)));
        slot->setType(types_.pointerTo(charPtr));
        ExprPtr there(new Unary('*', std::move(slot)));
        there->setType(charPtr);
        ExprPtr store(new Assign(std::move(there), std::move(value)));
        store->setType(charPtr);
        into.push_back(StmtPtr(new ExprStmt(std::move(store))));
    }
}

// **Setting the vptr, for whoever is building the object**, pulled out of the
// constructor path when implicit constructors arrived. Itanium's whole walk
// is ParserVtable.cpp's; Microsoft has one table, and no header before it.
std::vector<StmtPtr> Parser::storeVptrs(const std::string &cls,
                                        const Type *memberOf, int thisSlot) {
    std::vector<StmtPtr> withVptr;
    if (!target_.microsoftNames())
        return itaniumVptrStores(cls, memberOf, thisSlot);
    // **Microsoft has two pointers and a class can want either alone.**
    if (!memberOf->polymorphic()) return withVptr;
    // **The class itself where it is the one named**, so a specialization's
    // table is spelled by the mangler rather than by counting the letters of
    // `P<int>`.
    const std::string table =
        memberOf != nullptr && memberOf->unqualified()->tag() == cls
            ? vtableSymbol(memberOf, true) : vtableSymbol(cls, true);
    const Type *entry = types_.pointerTo(types_.get(Kind::Void));
    const Type *entries = types_.pointerTo(entry);
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));

    // One store per vfptr, each its own table (Phase 6).
    const std::vector<MsVftable> tables = msVftables(memberOf);
    for (const MsVftable &t : tables) {
        // **The table's ADDRESS, not its contents.** A global Var is an lvalue and
        // reading one loads from it, which stored the table's first word in the vptr and
        // crashed on the first call. Giving it the array type and decaying it is it.
        const std::string name = table.substr(0, table.size() - 3) + "6B" + microsoftVftablePath(t.path) + "@";
        ExprPtr base(Var::global(name));
        base->setType(types_.arrayOf(entry, static_cast<long long>(t.slots.size())));
        ExprPtr value = decay(std::move(base));
        ExprPtr asVoid(new Cast(entry, std::move(value)));
        asVoid->setType(entry);

        ExprPtr self(Var::local("this", thisSlot));
        self->setType(entries);
        if (t.vbase != nullptr) {
            ExprPtr me(Var::local("this", thisSlot));
            me->setType(types_.pointerTo(memberOf));
            ExprPtr there = msShifted(convert(std::move(me), types_.pointerTo(t.vbase)), t.atInVbase);
            self.reset(new Cast(entries, std::move(there)));
            self->setType(entries);
        } else if (t.at != 0) {
            ExprPtr asChars(new Cast(chars, std::move(self)));
            asChars->setType(chars);
            ExprPtr off(new Num(static_cast<long long>(t.at)));
            off->setType(types_.intType());
            ExprPtr moved(new Binary(BinOp::Add, std::move(asChars), std::move(off)));
            moved->setType(chars);
            self.reset(new Cast(entries, std::move(moved)));
            self->setType(entries);
        }
        ExprPtr where(new Unary('*', std::move(self)));
        where->setType(entry);

        ExprPtr store(new Assign(std::move(where), std::move(asVoid)));
        store->setType(entry);
        withVptr.push_back(StmtPtr(new ExprStmt(std::move(store))));
    }
    // **And each vtordisp**, how far the virtual base is from where this class alone would put it: 0
    // in a complete object, the difference in one built as a base - cl's B::B, `vbtable[1] - 24`.
    std::map<std::string, std::set<const Type *> >::const_iterator vd = msVtordisp_.find(memberOf->tag());
    if (vd != msVtordisp_.end())
        for (const Type *v : vd->second) {
            int staticAt = 0;
            for (const Type::BaseSpec &b : memberOf->bases())
                if (b.isVirtual && b.type->unqualified() == v) staticAt = b.offset;
            const Type *self = types_.pointerTo(memberOf);
            ExprPtr me(Var::local("this", thisSlot));
            me->setType(self);
            ExprPtr vb(new Cast(chars, convert(std::move(me), types_.pointerTo(v))));
            vb->setType(chars);
            const int vbSlot = allocateFrameSlot(chars);
            const std::string vbTemp = ".vtd" + std::to_string(refTemps_++);
            ExprPtr held(Var::local(vbTemp, vbSlot));
            held->setType(chars);
            ExprPtr keep(new Assign(std::move(held), std::move(vb)));
            keep->setType(chars);
            withVptr.push_back(StmtPtr(new ExprStmt(std::move(keep))));
            ExprPtr a(Var::local(vbTemp, vbSlot));
            a->setType(chars);
            ExprPtr me2(Var::local("this", thisSlot));
            me2->setType(self);
            ExprPtr b(new Cast(chars, std::move(me2)));
            b->setType(chars);
            ExprPtr diff(new Binary(BinOp::Sub, std::move(a), std::move(b)));
            diff->setType(ptrdiffType());
            ExprPtr minus(new Num(static_cast<long long>(staticAt)));
            minus->setType(ptrdiffType());
            ExprPtr value(new Binary(BinOp::Sub, std::move(diff), std::move(minus)));
            value->setType(ptrdiffType());
            ExprPtr asInt(new Cast(types_.intType(), std::move(value)));
            asInt->setType(types_.intType());
            ExprPtr there(Var::local(vbTemp, vbSlot));
            there->setType(chars);
            ExprPtr four(new Num(-4LL));
            four->setType(types_.intType());
            ExprPtr at(new Binary(BinOp::Add, std::move(there), std::move(four)));
            at->setType(chars);
            const Type *intPtr = types_.pointerTo(types_.intType());
            ExprPtr slotPtr(new Cast(intPtr, std::move(at)));
            slotPtr->setType(intPtr);
            ExprPtr field(new Unary('*', std::move(slotPtr)));
            field->setType(types_.intType());
            ExprPtr set(new Assign(std::move(field), std::move(asInt)));
            set->setType(types_.intType());
            withVptr.push_back(StmtPtr(new ExprStmt(std::move(set))));
        }
    return withVptr;
}

// **-g: the name a debugger shows for a member the parser made**, as cl's PDB
// has it (`geo::Pair::Pair`, `` Tally::`scalar deleting destructor' ``): the
// class's whole name, then the member; the Function keeps `tag::member`.
static void showMemberAs(Function &f, const Type *cls, const std::string &member) {
    f.setDisplayName(Type::displaySpelling(cls->displayTag() + "::" + member));
}

void Parser::synthesizeDeleting(const std::string &cls, const Type *type,
                                Access access, std::size_t pos) {
    const bool ms = target_.microsoftNames();
    const Type *self = types_.pointerTo(type);
    const std::string symbol = deletingDestructorSymbol(cls);
    (void)access;

    // Its own frame: `this`, and on Windows the flag beside it.
    const FrameSave savedFrame = openFrame();
    std::vector<Param> params;
    int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    int flagSlot = 0;
    const Type *flagType = types_.get(Kind::UInt);
    if (ms) {
        flagSlot = allocateFrameSlot(flagType);
        params.push_back(Param{ flagType, flagSlot });
    }

    std::vector<StmtPtr> body;
    msEnterAdjusted(body, thisSlot, self, symbol);

    const Signature *dtor = destructorOf(type);
    if (dtor != nullptr) {
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        body.push_back(StmtPtr(new ExprStmt(destructorCall(std::move(me), *dtor, pos))));
    }

    // operator delete(this)
    ExprPtr again(Var::local("this", thisSlot));
    again->setType(self);
    const Type *vp = types_.pointerTo(types_.get(Kind::Void));
    ExprPtr raw(new Cast(vp, std::move(again)));
    raw->setType(vp);
    StmtPtr freeIt(new ExprStmt(deallocate(type, std::move(raw), pos)));

    if (ms) {
        // if (flags & 1) operator delete(this);
        ExprPtr flags(Var::local("flags", flagSlot));
        flags->setType(flagType);
        ExprPtr one(new Num(1LL));
        one->setType(flagType);
        ExprPtr test(new Binary(BinOp::BitAnd, std::move(flags), std::move(one)));
        test->setType(flagType);
        body.push_back(StmtPtr(new If(std::move(test), std::move(freeIt), nullptr)));

        ExprPtr back(Var::local("this", thisSlot));
        back->setType(self);
        ExprPtr asVoid(new Cast(vp, std::move(back)));
        asVoid->setType(vp);
        body.push_back(StmtPtr(new Return(std::move(asVoid))));
    } else {
        body.push_back(std::move(freeIt));
        body.push_back(StmtPtr(new Return(nullptr)));
    }

    const Type *returns = ms ? vp : types_.get(Kind::Void);
    current_->functions.push_back(Function(cls + "::deleting", returns,
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos,
                                           std::vector<::Local>()));
    showMemberAs(current_->functions.back(), type, "`scalar deleting destructor'");
    current_->functions.back().setSymbol(symbol);
    // **A compiler-written special member is inline** - [class.copy] and its
    // neighbours say the implicit definition is - so several translation units
    // may each hold one and the linker folds them.
    current_->functions.back().setInline(true);
    closeFrame(savedFrame);
}

// **What cl's most-derived flag guards**: the vbtable pointers this class
// stores and the virtual bases it builds.
std::vector<StmtPtr> Parser::guardedVirtualBaseInit(
        const Type *type, const std::string &cls, int thisSlot, int flagSlot,
        std::size_t pos, int srcSlot, bool moving,
        std::map<std::string, std::vector<ExprPtr> > *vbaseArgs,
        std::vector<Alive> *vbs) {
    std::vector<StmtPtr> out;
    if (!target_.microsoftNames() || !type->hasVirtualBase() || flagSlot < 0)
        return out;

    std::vector<StmtPtr> inside;
    storeVbptr(cls, type, thisSlot, inside);
    std::vector<int> builtBase;
    std::vector<StmtPtr> built = virtualBaseCalls(type, thisSlot, true, pos,
                                                  srcSlot, moving, vbaseArgs, &builtBase);
    const int count = vbs != nullptr ? allocateFrameSlot(types_.intType()) : 0;
    auto counted = [&](long long n) {
        ExprPtr slot(Var::local("$vb", count));
        slot->setType(types_.intType());
        ExprPtr value(new Num(n));
        value->setType(types_.intType());
        ExprPtr set(new Assign(std::move(slot), std::move(value)));
        set->setType(types_.intType());
        return StmtPtr(new ExprStmt(std::move(set)));
    };
    for (std::size_t i = 0; i < built.size(); i++) {
        inside.push_back(std::move(built[i]));
        if (vbs == nullptr || builtBase[i] < 0) continue;
        Alive vb{ "this", thisSlot, type };
        vb.owner = type;
        vb.base = builtBase[i];
        vb.whenSlot = count;
        vb.whenAbove = static_cast<int>(vbs->size());
        vbs->push_back(vb);
        inside.push_back(counted(static_cast<long long>(vbs->size())));
    }
    if (inside.empty()) return out;
    if (vbs != nullptr) out.push_back(counted(0));

    ExprPtr flag(Var::local(".initVBases", flagSlot));
    flag->setType(types_.intType());
    out.push_back(StmtPtr(new If(std::move(flag),
                                 StmtPtr(new Block(std::move(inside))),
                                 StmtPtr())));
    return out;
}

// The virtual bases of `type`, built or destroyed through `this`.
std::vector<StmtPtr> Parser::virtualBaseCalls(const Type *type, int thisSlot,
                                              bool building, std::size_t pos,
                                              int srcSlot, bool moving,
                                              std::map<std::string,
                                                  std::vector<ExprPtr> >
                                                  *vbaseArgs,
                                              std::vector<int> *builtBase) {
    std::vector<StmtPtr> out;
    // **[class.base.init]/10's order, which is not the layout's**: a base's
    // own virtual bases come before it, each direct base in turn - clang's
    // vbases(), and V before the W that names V virtually.
    struct Order {
        static void of(const Type *t, std::vector<const Type::BaseSpec *> &out,
                       std::set<const Type *> &seen) {
            const std::vector<const Type::BaseSpec *> ds = t->directBases();
            for (std::size_t i = 0; i < ds.size(); i++) {
                of(ds[i]->type, out, seen);
                if (ds[i]->isVirtual && seen.insert(ds[i]->type).second)
                    out.push_back(ds[i]);
            }
        }
    };
    std::vector<const Type::BaseSpec *> found;
    std::set<const Type *> seen;
    Order::of(type, found, seen);
    std::vector<Type::BaseSpec> bs;
    std::vector<int> bsIndex;
    for (std::size_t i = 0; i < found.size(); i++) {
        const Type *b = found[i]->type;
        const std::vector<Type::BaseSpec> &all = type->bases();
        for (std::size_t k = 0; k < all.size(); k++)
            if (all[k].isVirtual && all[k].type == b) {
                bs.push_back(all[k]);
                bsIndex.push_back(static_cast<int>(k));
            }
    }
    const Type *self = types_.pointerTo(type);
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    for (std::size_t n = 0; n < bs.size(); n++) {
        const std::size_t i = building ? n : bs.size() - 1 - n;
        if (!bs[i].isVirtual) continue;
        const Type *base = bs[i].type;
        if (base->tag().empty()) continue;

        const Type *basePtr = types_.pointerTo(base);
        ExprPtr me(Var::local("this", thisSlot));
        ExprPtr addr;
        if (bs[i].offset == 0) {
            me->setType(basePtr);
            addr = std::move(me);
        } else {
            me->setType(self);
            ExprPtr asChars(new Cast(chars, std::move(me)));
            asChars->setType(chars);
            ExprPtr step(new Num(static_cast<long long>(bs[i].offset)));
            step->setType(types_.get(Kind::LongLong));
            ExprPtr moved(new Binary(BinOp::Add, std::move(asChars),
                                     std::move(step)));
            moved->setType(chars);
            addr = ExprPtr(new Cast(basePtr, std::move(moved)));
            addr->setType(basePtr);
        }

        std::string symbol;
        ExprPtr srcArg;
        const Type *srcParam = nullptr;
        Signature namedCtor;
        const Signature *ctor = nullptr;
        std::vector<ExprPtr> ctorArgs;
        if (building) {
            // **A copy builds its virtual base from the source's**, not from
            // nothing: `Dia b(a)` copy-constructs the one `V`, which is what
            // clang emits.
            std::map<std::string, std::vector<ExprPtr> >::iterator said =
                vbaseArgs == nullptr ? std::map<std::string,
                    std::vector<ExprPtr> >::iterator()
                                     : vbaseArgs->find(base->tag());
            if (vbaseArgs != nullptr && said != vbaseArgs->end() &&
                !said->second.empty()) {
                ctorArgs.swap(said->second);
                namedCtor = resolveOverload(constructorKey(base->tag()),
                                            ctorArgs, pos);
                applyDefaults(namedCtor, ctorArgs, pos);
                ctor = &namedCtor;
            } else if (srcSlot >= 0) {
                if (moving) ctor = moveConstructorOf(base);
                if (ctor == nullptr) ctor = copyConstructorOf(base);
                // A trivial virtual base has no constructor to call; its bytes
                // travel with the member walk in C2, as they do today.
                if (ctor == nullptr) continue;
                markUsed(ctor);
            } else {
                ctor = defaultConstructorOf(base);
                if (ctor == nullptr) continue;
                markUsed(ctor);
            }
            // **The base-subobject name, where the ABI has one.** Itanium
            // spells C2 differently from C1; Microsoft has one name and says
            // "subobject" with the flag instead, which `completeCall` adds.
            if (!target_.microsoftNames()) {
                symbol = baseConstructorSymbol(*ctor, base);
            } else {
                symbol = ctor->symbol;
            }
            if (srcSlot >= 0 && !ctor->params.empty()) {
                srcParam = ctor->params[0];
                ExprPtr that(Var::local("that", srcSlot));
                that->setType(self);
                ExprPtr asChars2(new Cast(chars, std::move(that)));
                asChars2->setType(chars);
                ExprPtr step2(new Num(static_cast<long long>(bs[i].offset)));
                step2->setType(types_.get(Kind::LongLong));
                ExprPtr moved2(new Binary(BinOp::Add, std::move(asChars2),
                                          std::move(step2)));
                moved2->setType(chars);
                ExprPtr at(new Cast(basePtr, std::move(moved2)));
                at->setType(basePtr);
                ExprPtr obj(new Unary('*', std::move(at)));
                obj->setType(base);
                if (moving) obj->setXvalue();
                srcArg = std::move(obj);
            }
        } else {
            const Signature *dtor = destructorOf(base);
            if (dtor == nullptr) continue;
            markUsed(dtor);
            // D2 on Itanium; on Microsoft `??1`, which is D2 there - the
            // virtual base's *own* virtual bases, if it has any, belong to
            // this class and not to it.
            if (!target_.microsoftNames())
                itaniumDestructorName(base->tag(), base, false, &symbol);
            else
                symbol = dtor->symbol;
        }

        std::vector<ExprPtr> args;
        args.push_back(std::move(addr));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        // A virtual base with virtual bases of its own reads its vptrs from
        // this class's VTT, the part written for it.
        if (takesVtt(base)) {
            const VttLayout &vtt = vtts_[type->tag()];
            std::map<const Type *, int>::const_iterator part =
                vtt.virtualVtt.find(base);
            if (part == vtt.virtualVtt.end())
                src_.fail(pos, "'" + type->tag() + "' has no virtual VTT for '" +
                               base->tag() + "'");
            args.push_back(vttOfClass(type, part->second));
            ps.push_back(vttType());
        }
        if (srcArg != nullptr) {
            args.push_back(std::move(srcArg));
            ps.push_back(srcParam);
        }
        // **The mem-initialiser's own arguments**, converted to the parameters
        // the chosen constructor declared, the way every other call converts
        // them. `ctorArgs` is empty for the default and copy paths above.
        for (std::size_t k = 0; k < ctorArgs.size(); k++) {
            const Type *want = k < ctor->params.size() ? ctor->params[k]
                                                       : ctorArgs[k]->type();
            ExprPtr one = decay(std::move(ctorArgs[k]));
            checkAssignable(*one, want, pos, "'" + base->tag() + "'");
            args.push_back(convert(std::move(one), want));
            ps.push_back(want);
        }
        // **A virtual base is a subobject and not the complete object**, so
        // the flag it is handed is 0: anything *it* has virtually is this
        // class's to build as well.
        out.push_back(StmtPtr(new ExprStmt(
            completeCall(base->tag(), symbol, nullptr, types_.get(Kind::Void),
                         ps, false, pos, std::move(args), false, 0))));
        if (builtBase != nullptr)
            builtBase->push_back(building && destructorOf(base) != nullptr ? bsIndex[i] : -1);
    }
    return out;
}

void Parser::synthesizeCompleteCtor(const Type *type,
                                    const std::vector<const Type *> &ctorParams,
                                    const std::string &c1, const std::string &c2,
                                    bool isInline, std::size_t pos,
                                    int copyArg, bool moving,
                                    std::map<std::string,
                                        std::vector<ExprPtr> > *vbaseArgs, bool delegating) {
    const std::string &cls = type->tag();
    const Type *self = types_.pointerTo(type);

    const FrameSave savedFrame = openFrame();
    // **The arguments take their slots before `this`, which is C2's order and
    // not the obvious one.**
    std::vector<int> argSlots;
    std::vector<const Type *> argHeld;
    for (std::size_t i = 0; i < ctorParams.size(); i++) {
        const Type *held = ctorParams[i]->isReference()
                         ? types_.pointerTo(ctorParams[i]->referent())
                         : ctorParams[i];
        argSlots.push_back(allocateFrameSlot(held));
        argHeld.push_back(held);
    }
    const int thisSlot = allocateFrameSlot(self);
    std::vector<Param> params;
    params.push_back(Param{ self, thisSlot });
    for (std::size_t i = 0; i < ctorParams.size(); i++)
        params.push_back(Param{ argHeld[i], argSlots[i] });

    const int srcSlot = (copyArg >= 0 &&
                         static_cast<std::size_t>(copyArg) < argSlots.size())
                      ? argSlots[copyArg] : -1;
    std::vector<int> builtBase;
    // A delegating constructor's target builds them, in the C1 it reaches through its entry.
    std::vector<StmtPtr> body;
    if (!delegating) body = virtualBaseCalls(type, thisSlot, true, pos, srcSlot, moving, vbaseArgs, &builtBase);

    // Then C2, which builds everything else - the non-virtual bases, the
    // members, and the body the user wrote.
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    std::vector<ExprPtr> args;
    args.push_back(std::move(me));
    std::vector<const Type *> ps;
    ps.push_back(self);
    if (takesVtt(type)) {
        args.push_back(vttOfClass(type, 0));
        ps.push_back(vttType());
    }
    for (std::size_t i = 0; i < ctorParams.size(); i++) {
        ExprPtr a(Var::local("a", argSlots[i]));
        if (ctorParams[i]->isReference()) {
            // The slot holds the address; the callee wants the object, bound
            // to its reference parameter the way every other call binds one.
            a->setType(types_.pointerTo(ctorParams[i]->referent()));
            ExprPtr obj(new Unary('*', std::move(a)));
            obj->setType(ctorParams[i]->referent());
            if (moving) obj->setXvalue();
            a = std::move(obj);
        } else {
            a->setType(ctorParams[i]);
        }
        args.push_back(std::move(a));
        ps.push_back(ctorParams[i]);
    }
    // [except.ctor]/3 for the virtual bases: what follows each one's construction destroys it on
    // unwind. This function is written on its own, so no enclosing `try` is any of its business.
    std::vector<Alive> done(body.size());
    for (std::size_t i = 0; i < body.size(); i++)
        if (builtBase[i] >= 0) {
            done[i] = Alive{ "this", thisSlot, type };
            done[i].owner = type;
            done[i].base = builtBase[i];
        }
    body.push_back(StmtPtr(new ExprStmt(
        completeCall(cls, c2, nullptr, types_.get(Kind::Void), ps, false, pos,
                     std::move(args)))));
    bool guarded = false;
    StmtPtr whole = guardConstruction(std::move(body), std::move(done), pos, &guarded);
    body.clear();
    body.push_back(std::move(whole));

    current_->functions.push_back(Function(cls + "::complete",
                                           types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos,
                                           std::vector<::Local>()));
    current_->functions.back().setSymbol(c1);
    current_->functions.back().setHasLandingPads(guarded);
    if (isInline) current_->functions.back().setInline(true);
    showMemberAs(current_->functions.back(), type, localOf(cls));
    closeFrame(savedFrame);
}

// **The entry a delegating constructor calls** where the class has virtual bases: it has one `this` and one list of
// arguments and must be its target's C1 for a complete object and its target's C2 for a base. A complete object's
// C1 hands down the class's own VTT and a base's the part of a larger one, so the VTT says which this is.
void Parser::synthesizeDelegationEntry(const Type *type, const DelegationEntry &d, std::size_t pos) {
    const std::string &cls = type->tag();
    const Type *self = types_.pointerTo(type);
    const FrameSave savedFrame = openFrame();
    const int thisSlot = allocateFrameSlot(self);
    const int vttSlot = allocateFrameSlot(vttType());
    std::vector<Param> params;
    params.push_back(Param{ self, thisSlot });
    params.push_back(Param{ vttType(), vttSlot });
    std::vector<int> slots;
    for (std::size_t i = 0; i < d.params.size(); i++) {
        const Type *held = d.params[i]->isReference() ? types_.pointerTo(d.params[i]->referent()) : d.params[i];
        slots.push_back(allocateFrameSlot(held));
        params.push_back(Param{ held, slots[i] });
    }
    // One call of either constructor: this, the VTT if it takes one, and the arguments as they came.
    const auto calling = [&](const std::string &symbol, bool withVtt) {
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(self);
        if (withVtt) {
            ExprPtr vtt(Var::local(".vtt", vttSlot));
            vtt->setType(vttType());
            args.push_back(std::move(vtt));
            ps.push_back(vttType());
        }
        for (std::size_t i = 0; i < d.params.size(); i++) {
            ExprPtr a(Var::local("a", slots[i]));
            if (d.params[i]->isReference()) {
                a->setType(types_.pointerTo(d.params[i]->referent()));
                ExprPtr obj(new Unary('*', std::move(a)));
                obj->setType(d.params[i]->referent());
                if (d.params[i]->isRValueReference()) obj->setXvalue();
                a = std::move(obj);
            } else {
                a->setType(d.params[i]);
            }
            args.push_back(std::move(a));
            ps.push_back(d.params[i]);
        }
        return StmtPtr(new ExprStmt(completeCall(cls, symbol, nullptr, types_.get(Kind::Void), ps, false, pos, std::move(args))));
    };
    ExprPtr vtt(Var::local(".vtt", vttSlot));
    vtt->setType(vttType());
    ExprPtr own(new Cast(vttType(), vttOfClass(type, 0)));
    own->setType(vttType());
    ExprPtr complete(new Binary(BinOp::Eq, std::move(vtt), std::move(own)));
    complete->setType(types_.intType());
    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new If(std::move(complete), calling(d.c1, false), calling(d.c2, true))));
    current_->functions.push_back(Function(cls + "::entry", types_.get(Kind::Void), std::move(params),
                                           StmtPtr(new Block(std::move(body))), alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(d.entry);
    current_->functions.back().setInline(true);
    showMemberAs(current_->functions.back(), type, localOf(cls));
    closeFrame(savedFrame);
}

void Parser::synthesizeCompleteDtor(const Type *type, const std::string &d1,
                                    const std::string &d2, bool isInline,
                                    std::size_t pos) {
    const std::string &cls = type->tag();
    const Type *self = types_.pointerTo(type);

    const FrameSave savedFrame = openFrame();
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });

    // D2 first - it destroys what C2 built - and the virtual bases after it.
    std::vector<StmtPtr> body;
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    std::vector<ExprPtr> args;
    args.push_back(target_.microsoftNames() ? msShifted(std::move(me), msThisAdjust(d2)) : std::move(me));
    std::vector<const Type *> ps;
    ps.push_back(self);
    if (takesVtt(type)) {
        args.push_back(vttOfClass(type, 0));
        ps.push_back(vttType());
    }
    body.push_back(StmtPtr(new ExprStmt(
        completeCall(cls, d2, nullptr, types_.get(Kind::Void), ps, false, pos,
                     std::move(args)))));

    std::vector<StmtPtr> after = virtualBaseCalls(type, thisSlot, false, pos);
    for (std::size_t i = 0; i < after.size(); i++)
        body.push_back(std::move(after[i]));

    current_->functions.push_back(Function(cls + "::completeDtor",
                                           types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos,
                                           std::vector<::Local>()));
    current_->functions.back().setSymbol(d1);
    if (isInline) current_->functions.back().setInline(true);
    showMemberAs(current_->functions.back(), type, "~" + localOf(cls));
    closeFrame(savedFrame);
}

const Parser::Signature *Parser::destructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(destructorKey(cls->tag()));
    return set == nullptr ? nullptr : &functions_[(*set)[0]];
}

// One destructor call, given the address of what to destroy. A destructor
// takes nothing but `this`, so this is the smallest call the compiler makes.
ExprPtr Parser::destructorCall(ExprPtr address, const Signature &dtor,
                               std::size_t pos) {
    refuseDeleted(dtor, pos);
    // Calling one is what asks for a body, which is the only thing that makes
    // an implicit destructor a function at all.
    markUsed(&dtor);
    std::vector<ExprPtr> args;
    // **A complete object of a Microsoft class with a virtual base is
    // destroyed through `??_D`**, which destroys the class's own part and then
    // the virtual bases - Itanium's D1.
    std::string symbol = dtor.symbol;
    if (target_.microsoftNames() && msVbaseClasses_.count(dtor.owner) != 0) {
        symbol = vbaseDestructorSymbol(dtor.owner);
        markSymbolUsed(symbol);
    }
    // ??1 itself, where it is entered at a virtual base, takes `this` there (M3).
    if (target_.microsoftNames()) address = msShifted(std::move(address), msThisAdjust(symbol));
    args.push_back(std::move(address));
    std::vector<const Type *> params;
    params.push_back(args[0]->type());
    return completeCall("~" + dtor.owner, symbol, nullptr,
                        types_.get(Kind::Void), params, false, pos,
                        std::move(args));
}

// **RAII is this function**: everything constructed since `from` is destroyed, last
// first. **One region per stretch, and the stretches do not overlap** - which is what
// lets a call-site table hold them, and what the Microsoft chain walks backwards.
std::vector<StmtPtr> Parser::wrapMsCleanups(
    std::vector<StmtPtr> body,
    const std::vector<std::pair<std::size_t, std::size_t> > &built,
    std::size_t aliveAtEntry, std::size_t pos,
    const std::vector<Temporary> &temps) {
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    const int pointerSlot = allocateFrameSlot(voidPtr);
    const int selectorSlot = allocateFrameSlot(types_.intType());
    const int helpSlot = msUnwindHelp();
    functionHasPads_ = true;

    std::vector<StmtPtr> out;
    for (std::size_t i = 0; i < built[0].first; i++)
        out.push_back(std::move(body[i]));

    for (std::size_t k = 0; k < built.size(); k++) {
        const std::size_t from = built[k].first;
        const std::size_t to = k + 1 < built.size() ? built[k + 1].first
                                                    : body.size();
        std::vector<StmtPtr> guarded;
        for (std::size_t i = from; i < to; i++) guarded.push_back(std::move(body[i]));
        if (guarded.empty()) continue;

        std::vector<StmtPtr> steps;
        // The block's temporaries, each under its guard - see cleanupPad. The
        // funclets chain, so clearing as it destroys is what stops the next
        // one in the chain destroying the same object again.
        for (std::size_t j = temps.size(); j-- > 0; )
            releaseGuarded(steps, temps[j]);
        emitDestructors(steps, k == 0 ? aliveAtEntry : built[k - 1].second,
                        pos, -1, built[k].second);
        Block *b = new Block(std::move(steps));
        b->setScope(-1);

        Try *t = new Try(std::move(guarded), nullptr, pointerSlot,
                         selectorSlot, std::vector<std::string>());
        t->setCleanup(StmtPtr(b));
        t->setUnwindHelpSlot(helpSlot);
        out.push_back(StmtPtr(t));
    }
    return out;
}

std::vector<StmtPtr> Parser::wrapCleanups(
    std::vector<StmtPtr> body,
    const std::vector<std::pair<std::size_t, std::size_t> > &built,
    std::size_t aliveAtEntry, std::size_t pos,
    const std::vector<Temporary> &temps,
    const std::vector<std::size_t> &tryAt) {
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    // **Inside a `try` body the slots are the `try`'s.**
    int targetPtr = 0, targetSel = 0;
    const std::string handOver = unwindTarget(&targetPtr, &targetSel);
    const bool intoTry = !handOver.empty();
    const int pointerSlot = intoTry ? targetPtr : allocateFrameSlot(voidPtr);
    const int selectorSlot = intoTry ? targetSel
                                     : allocateFrameSlot(types_.intType());
    functionHasPads_ = true;

    std::vector<StmtPtr> out;
    for (std::size_t i = 0; i < built[0].first; i++)
        out.push_back(std::move(body[i]));

    for (std::size_t k = 0; k < built.size(); k++) {
        const std::size_t from = built[k].first;
        const std::size_t to = k + 1 < built.size() ? built[k + 1].first
                                                    : body.size();
        // **A region is split around every `try` in it.**
        std::size_t cur = from;
        while (cur < to) {
            std::size_t stop = to;
            for (std::size_t t = 0; t < tryAt.size(); t++)
                if (tryAt[t] >= cur && tryAt[t] < stop) stop = tryAt[t];
            std::vector<StmtPtr> guarded;
            for (std::size_t i = cur; i < stop; i++)
                guarded.push_back(std::move(body[i]));
            if (!guarded.empty()) {
                // **From the `try`'s entry, not this block's.**
                Try *seg = new Try(
                    std::move(guarded),
                    // what the pad destroys: everything since the `try` or the handler it hands over to was
                    // entered - it jumps there and does not resume, so no enclosing pad runs after it
                    cleanupPad(!intoTry ? aliveAtEntry
                               : handOver == tryChainLabel_ ? tryChainAliveFrom_ : handlerAliveFrom_,
                               built[k].second, pointerSlot,
                               temps, pos, handOver),
                    pointerSlot, selectorSlot, std::vector<std::string>());
                // The types are the `try`'s and are not read yet; tryStatement
                // patches every segment once its handlers have been.
                if (intoTry && handOver == tryChainLabel_)
                    tryBodySegments_.push_back(seg);
                out.push_back(StmtPtr(seg));
            }
            if (stop == to) break;
            out.push_back(std::move(body[stop]));      // the try, uncovered
            cur = stop + 1;
        }
    }
    return out;
}

// **One statement list under a cleanup of its own** - `onUnwind` runs as an
// exception leaves it, then unwinding goes on: a static local's guard reset on
// a throw, [stmt.dcl]/4 (Microsoft: a funclet state; Itanium: a pad).
StmtPtr Parser::cleanupRegion(std::vector<StmtPtr> body, StmtPtr onUnwind) {
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    functionHasPads_ = true;
    std::vector<StmtPtr> steps;
    steps.push_back(std::move(onUnwind));
    if (target_.microsoftNames()) {
        Block *b = new Block(std::move(steps));
        b->setScope(-1);
        Try *t = new Try(std::move(body), nullptr, allocateFrameSlot(voidPtr),
                         allocateFrameSlot(types_.intType()), std::vector<std::string>());
        t->setCleanup(StmtPtr(b));
        t->setUnwindHelpSlot(msUnwindHelp());
        return StmtPtr(t);
    }
    int targetPtr = 0, targetSel = 0;
    const std::string handOver = unwindTarget(&targetPtr, &targetSel);
    const bool intoTry = !handOver.empty();
    const int pointerSlot = intoTry ? targetPtr : allocateFrameSlot(voidPtr);
    const int selectorSlot = intoTry ? targetSel : allocateFrameSlot(types_.intType());
    if (intoTry) steps.push_back(StmtPtr(new Goto(handOver)));
    else         steps.push_back(resumeUnwinding(pointerSlot));
    Try *seg = new Try(std::move(body), unwindPad(std::move(steps)), pointerSlot, selectorSlot,
                       std::vector<std::string>());
    if (intoTry && handOver == tryChainLabel_) tryBodySegments_.push_back(seg);
    return StmtPtr(seg);
}

// **One temporary of a block, released on the way out of it, under its
// guard.**
void Parser::releaseGuarded(std::vector<StmtPtr> &steps, const Temporary &t) {
    if (t.flag == 0) return;
    std::vector<StmtPtr> both;
    if (t.exceptionStorage) {
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        std::vector<ExprPtr> args;
        ExprPtr held(Var::local("$copy", t.slot));
        held->setType(voidPtr);
        args.push_back(std::move(held));
        both.push_back(StmtPtr(new ExprStmt(
            runtimeCall("__cxa_free_exception", types_.get(Kind::Void),
                        std::move(args)))));
    } else if (t.newStorage) {
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        ExprPtr held(Var::local("$copy", t.slot));
        held->setType(chars);
        if (t.newCookie != 0) {
            // The allocation began a cookie before the first element.
            ExprPtr back(new Num(static_cast<long long>(-t.newCookie)));
            back->setType(types_.intType());
            ExprPtr start(new Binary(BinOp::Add, std::move(held), std::move(back)));
            start->setType(chars);
            held = std::move(start);
        }
        ExprPtr raw(new Cast(voidPtr, std::move(held)));
        raw->setType(voidPtr);
        ExprPtr give;
        if (t.placementDelete >= 0) {
            const PlacementDelete &pd = placementDeletes_[static_cast<std::size_t>(t.placementDelete)];
            std::vector<ExprPtr> args;
            args.push_back(std::move(raw));
            for (std::size_t i = 0; i < pd.slots.size(); i++) {
                ExprPtr kept(Var::local(pd.names[i], pd.slots[i]));
                kept->setType(pd.types[i]);
                args.push_back(std::move(kept));
            }
            give = completeCall(pd.fn.name, pd.fn.symbol, nullptr, pd.fn.returns, pd.fn.params, false, 0,
                                std::move(args));
        } else {
            const bool was = globalAllocation_;
            globalAllocation_ = t.newGlobal;
            give = t.newNothrow ? callNothrowDeallocator(t.newArray, std::move(raw))
                 : t.newArray  ? deallocateArray(t.type, std::move(raw), 0)
                               : deallocate(t.type, std::move(raw), 0);
            globalAllocation_ = was;
        }
        both.push_back(StmtPtr(new ExprStmt(std::move(give))));
    } else if (t.newElement) {
        const Signature *dtor = destructorOf(t.type);
        if (dtor == nullptr) return;
        both.push_back(StmtPtr(new ExprStmt(destructorCall(temporaryAddress(t), *dtor, 0))));
    } else {
        const Signature *dtor = destructorOf(t.type);
        if (dtor == nullptr) return;
        ExprPtr what(Var::local("$copy", t.slot));
        what->setType(t.type);
        ExprPtr at(new Unary('&', std::move(what)));
        at->setType(types_.pointerTo(t.type));
        both.push_back(StmtPtr(new ExprStmt(
            destructorCall(std::move(at), *dtor, 0))));
    }
    both.insert(both.begin(), StmtPtr(new ExprStmt(setGuard(t.flag, 0))));
    ExprPtr live(Var::local("$guard", t.flag));
    live->setType(types_.intType());
    steps.push_back(StmtPtr(new If(std::move(live),
                                   StmtPtr(new Block(std::move(both))),
                                   StmtPtr())));
}

// **What an exception has to do on its way out of a scope.** The objects are the ones
// a `return` unwinds - `alive_` holds them and nothing new had to track them - and
// the difference is where the code runs from: a pad, ending in _Unwind_Resume.
StmtPtr Parser::cleanupPad(std::size_t from, std::size_t to, int pointerSlot,
                           const std::vector<Temporary> &temps,
                           std::size_t pos, const std::string &chainLabel) {
    // **Bounded rather than truncated.**
    std::vector<StmtPtr> steps;
    // **The temporaries of this block go first**, being the most recently
    // built - and each under its own guard, because the pad may be reached
    // from a point in the statement where this one does not exist yet.
    for (std::size_t k = temps.size(); k-- > 0; )
        releaseGuarded(steps, temps[k]);
    emitDestructors(steps, from, pos, -1, to);

    // **A segment of a `try` body hands over rather than resuming.** Its row
    // carries the `try`'s catch types, so the selector may name a handler -
    // and the one chain that tests it lives in the `try`'s own pad.
    if (chainLabel.empty()) steps.push_back(resumeUnwinding(pointerSlot));
    else                    steps.push_back(StmtPtr(new Goto(chainLabel)));
    return unwindPad(std::move(steps));
}

StmtPtr Parser::resumeUnwinding(int pointerSlot) {
    std::vector<ExprPtr> args;
    if (target_.resumeTakesException()) {
        ExprPtr ptr(Var::local(".ex.ptr", pointerSlot));
        ptr->setType(types_.pointerTo(types_.get(Kind::Void)));
        args.push_back(std::move(ptr));
        return StmtPtr(new ExprStmt(runtimeCall("_Unwind_Resume", types_.get(Kind::Void), std::move(args))));
    }
    return StmtPtr(new ExprStmt(runtimeCall("__cxa_end_cleanup", types_.get(Kind::Void), std::move(args))));
}

StmtPtr Parser::unwindPad(std::vector<StmtPtr> steps) {
    StmtPtr last = std::move(steps.back());
    steps.pop_back();
    Block *inner = new Block(std::move(steps));
    inner->setScope(-1);
    inner->setUnwindCleanup();
    std::vector<StmtPtr> outer;
    outer.push_back(StmtPtr(inner));
    outer.push_back(std::move(last));
    Block *b = new Block(std::move(outer));
    b->setScope(-1);
    return StmtPtr(b);
}

// **A destructor that throws has still ended its object**, [except.ctor]: the region over the end of
// the block would run it a second time on the way out, and every object built after it, gone by then,
// once more. Those are destroyed under a guard; what was built before it is still to go.
void Parser::guardThrowingDestructors(std::vector<StmtPtr> &body, std::size_t blockFrom, std::size_t from) {
    bool throwing = false;
    for (std::size_t i = blockFrom; i < from && !throwing; i++) throwing = alive_[i].whenOnce;
    for (std::size_t i = from; i < alive_.size(); i++) {
        Alive &a = alive_[i];
        if (a.owner != nullptr || a.builtSlot >= 0 || a.whenSlot >= 0) continue;
        throwing = throwing || destructorMayThrow(a.cls);
        if (!throwing) continue;
        a.whenSlot = guardFlag();
        a.whenOnce = true;
        body.push_back(StmtPtr(new ExprStmt(setGuard(a.whenSlot, 1))));
    }
}

void Parser::emitDestructors(std::vector<StmtPtr> &into, std::size_t from,
                             std::size_t pos, int except, std::size_t to) {
    if (to > alive_.size()) to = alive_.size();
    for (std::size_t i = to; i > from; i--) {
        const Alive &a = alive_[i - 1];
        if (except >= 0 && a.offset == except && !a.byAddress) continue;
        destroyObject(into, a, pos);
    }
}

// One object's destructor call, for the end of a scope and for every jump
// that leaves one early - a goto fills these in after its label is known,
// which is why this takes the record and not an index into alive_.
void Parser::destroyObject(std::vector<StmtPtr> &into, const Alive &a,
                           std::size_t pos) {
    if (a.whenSlot >= 0) {
        Alive plain = a;
        plain.whenSlot = -1;
        std::vector<StmtPtr> calls;
        if (a.whenOnce) calls.push_back(StmtPtr(new ExprStmt(setGuard(a.whenSlot, 0))));
        destroyObject(calls, plain, pos);
        ExprPtr n(Var::local(a.whenOnce ? "$guard" : "$vb", a.whenSlot));
        n->setType(types_.intType());
        ExprPtr above(new Num(static_cast<long long>(a.whenAbove)));
        above->setType(types_.intType());
        ExprPtr built(new Binary(BinOp::Gt, std::move(n), std::move(above)));
        built->setType(types_.intType());
        into.push_back(StmtPtr(new If(std::move(built), StmtPtr(new Block(std::move(calls))), StmtPtr())));
        return;
    }
    if (a.builtSlot >= 0) {
        // The first `$i` elements, from the array's first element: a member's, a pointer's or a slot's.
        const Type *ptr = types_.pointerTo(a.cls);
        ExprPtr base;
        if (a.owner != nullptr) {
            base = decay(thisMember(a.offset, a.owner, a.owner->members()[a.member]));
        } else if (a.byAddress) {
            base = ExprPtr(Var::local(a.name, a.offset));
            base->setType(ptr);
        } else {
            ExprPtr whole(Var::local(a.name, a.offset));
            whole->setType(a.cls);
            base = ExprPtr(new Unary('&', std::move(whole)));
            base->setType(ptr);
        }
        base = ExprPtr(new Cast(ptr, std::move(base)));
        base->setType(ptr);
        ExprPtr n(Var::local("$i", a.builtSlot));
        n->setType(types_.intType());
        into.push_back(StmtPtr(new ExprStmt(callVectorLoop(
            vectorDestructor(a.cls, pos), a.cls, std::move(base), std::move(n), pos))));
        return;
    }
    if (a.owner != nullptr) {
        if (a.member >= 0)
            memberDestructor(into, a.owner->tag(), a.owner, a.offset, static_cast<std::size_t>(a.member), pos);
        else if (a.base >= 0)
            into.push_back(baseDestructorCall(a.owner, static_cast<std::size_t>(a.base), a.offset, pos));
        else
            into.push_back(selfDestructorCall(a.owner, a.offset, pos));
        return;
    }
    const Signature *dtor = destructorOf(a.cls);
    if (dtor == nullptr) return;

    // An array: its elements last first, by the class's loop.
    if (a.count > 0) {
        ExprPtr first(Var::local(a.name, a.offset));
        first->setType(a.cls);
        ExprPtr base(new Unary('&', std::move(first)));
        base->setType(types_.pointerTo(a.cls));
        ExprPtr n(new Num(a.count));
        n->setType(types_.get(target_.sizeType()));
        into.push_back(StmtPtr(new ExprStmt(callVectorLoop(
            vectorDestructor(a.cls, pos), a.cls, std::move(base), std::move(n), pos))));
        return;
    }

    ExprPtr addr;
    if (a.byAddress) {
        // The slot holds the caller's pointer, and that pointer IS the
        // object's address.
        addr = ExprPtr(Var::local(a.name, a.offset));
        addr->setType(types_.pointerTo(a.cls));
    } else {
        ExprPtr object(Var::local(a.name, a.offset));
        object->setType(a.cls);
        addr = ExprPtr(new Unary('&', std::move(object)));
        addr->setType(types_.pointerTo(a.cls));
    }
    into.push_back(StmtPtr(new ExprStmt(destructorCall(std::move(addr),
                                                       *dtor, pos))));
}

// The vtable: one pointer per virtual function, in the order the base declared them,
// an override replacing an entry. **The two ABIs differ in the header** - Itanium 16
// bytes with a zero typeinfo, Microsoft none - and a thunk walks `this` back.
std::string Parser::synthesizeThunk(const std::string &cls, const Type *type,
                                    const VSlot &slot, int offset,
                                    std::size_t pos, const std::string &named,
                                    const Type *returnsOverride, bool vtordisp) {
    // _ZThn16_N1C1gEv.
    const std::string name = !named.empty() ? named
                                            : "_ZThn" + std::to_string(offset) + "_" + slot.symbol.substr(2);
    // One per name: a construction vtable names the same thunk again.
    for (std::size_t i = 0; i < current_->functions.size(); i++)
        if (current_->functions[i].symbol() == name) return name;

    const Type *self = types_.pointerTo(type);
    const FrameSave savedFrame = openFrame();

    std::vector<Param> params;
    int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    std::vector<int> argSlots;
    for (std::size_t i = 0; i < slot.params.size(); i++)
        argSlots.push_back(allocateFrameSlot(slot.params[i]));
    for (std::size_t i = 0; i < slot.params.size(); i++)
        params.push_back(Param{ slot.params[i], argSlots[i] });

    // (C *)((char *)this - offset)
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    ExprPtr asChars(new Cast(chars, std::move(me)));
    asChars->setType(chars);
    ExprPtr back(new Num(static_cast<long long>(-offset)));
    back->setType(types_.intType());
    // A vtordisp thunk takes the vtordisp in front of the virtual base off too:
    // `movsxd rax, [rcx-4]; sub rcx, rax`, as cl's $4 thunk begins.
    if (vtordisp) {
        ExprPtr me2(Var::local("this", thisSlot));
        me2->setType(self);
        ExprPtr chars2(new Cast(chars, std::move(me2)));
        chars2->setType(chars);
        ExprPtr four(new Num(-4LL));
        four->setType(types_.intType());
        ExprPtr at(new Binary(BinOp::Add, std::move(chars2), std::move(four)));
        at->setType(chars);
        const Type *intPtr = types_.pointerTo(types_.intType());
        ExprPtr asInt(new Cast(intPtr, std::move(at)));
        asInt->setType(intPtr);
        ExprPtr vd(new Unary('*', std::move(asInt)));
        vd->setType(types_.intType());
        const Type *wide = types_.get(Kind::LongLong);
        ExprPtr vdWide(new Cast(wide, std::move(vd)));
        vdWide->setType(wide);
        ExprPtr base(new Num(static_cast<long long>(-offset)));
        base->setType(wide);
        back.reset(new Binary(BinOp::Sub, std::move(base), std::move(vdWide)));
        back->setType(wide);
    }
    ExprPtr moved(new Binary(BinOp::Add, std::move(asChars), std::move(back)));
    moved->setType(chars);
    ExprPtr whole(new Cast(self, std::move(moved)));
    whole->setType(self);

    std::vector<ExprPtr> args;
    args.push_back(std::move(whole));
    std::vector<const Type *> full;
    full.push_back(self);
    for (std::size_t i = 0; i < slot.params.size(); i++) {
        ExprPtr a(Var::local("a" + std::to_string(i), argSlots[i]));
        a->setType(slot.params[i]);
        args.push_back(std::move(a));
        full.push_back(slot.params[i]);
    }

    const Signature *target = nullptr;
    if (const std::vector<std::size_t> *set = overloadsOf(cls + "::" + slot.name))
        for (std::size_t k = 0; k < set->size(); k++)
            if (functions_[(*set)[k]].symbol == slot.symbol) target = &functions_[(*set)[k]];
    const Type *returns = returnsOverride != nullptr ? returnsOverride
                        : target != nullptr ? target->returns : types_.get(Kind::Void);

    // A thunk forwards a member call and carries the same `this`, which is
    // what decides where the Microsoft ABI puts a hidden return pointer.
    ExprPtr call = completeCall(slot.name, slot.symbol, nullptr, returns, full,
                                false, pos, std::move(args), true);
    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new Return(returns->isVoid() ? nullptr : std::move(call))));
    if (returns->isVoid()) body.insert(body.begin(), StmtPtr(new ExprStmt(std::move(call))));

    current_->functions.push_back(Function(name, returns, std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(name);
    current_->functions.back().setInline(true);
    closeFrame(savedFrame);
    return name;
}

// The body is the virtual call `this->f(a0, a1, ...)` as memberCall builds
// one - the vptr read, the slot at its index - forwarding every parameter.
std::string Parser::synthesizeVcallThunk(const Type *cls, const Signature &f,
                                         int index, std::size_t pos) {
    std::string name, why;
    if (!microsoftVcallThunkName(cls, index * pointerBytes(), &name, &why))
        src_.fail(pos, "'&" + cls->tag() + "::" + f.name + "' cannot be named: " + why);
    for (std::size_t i = 0; i < current_->functions.size(); i++)
        if (current_->functions[i].symbol() == name) return name;   // one per slot

    const Type *self = types_.pointerTo(cls);
    const FrameSave savedFrame = openFrame();
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    std::vector<int> argSlots;
    for (std::size_t i = 0; i < f.params.size(); i++)
        argSlots.push_back(allocateFrameSlot(f.params[i]));
    for (std::size_t i = 0; i < f.params.size(); i++)
        params.push_back(Param{ f.params[i], argSlots[i] });

    std::vector<const Type *> full;
    full.push_back(self);
    for (std::size_t i = 0; i < f.params.size(); i++) full.push_back(f.params[i]);
    const Type *fnType = types_.functionType(f.returns, full, f.variadic);
    const Type *fnPtr = types_.pointerTo(fnType);
    const Type *table = types_.pointerTo(fnPtr);

    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    ExprPtr forLoad(new Cast(types_.pointerTo(table), std::move(me)));
    forLoad->setType(types_.pointerTo(table));
    ExprPtr vptr(new Unary('*', std::move(forLoad)));
    vptr->setType(table);
    if (index != 0) {
        ExprPtr at(new Num(static_cast<long long>(index) * fnPtr->size(target_)));
        at->setType(types_.intType());
        ExprPtr moved(new Binary(BinOp::Add, std::move(vptr), std::move(at)));
        moved->setType(table);
        vptr = std::move(moved);
    }
    ExprPtr entry(new Unary('*', std::move(vptr)));
    entry->setType(fnPtr);

    std::vector<ExprPtr> args;
    ExprPtr again(Var::local("this", thisSlot));
    again->setType(self);
    args.push_back(std::move(again));
    for (std::size_t i = 0; i < f.params.size(); i++) {
        ExprPtr a(Var::local("a" + std::to_string(i), argSlots[i]));
        a->setType(f.params[i]);
        args.push_back(std::move(a));
    }
    ExprPtr call = completeCall(f.name, f.symbol, std::move(entry), f.returns,
                                full, f.variadic, pos, std::move(args), true);
    std::vector<StmtPtr> body;
    if (f.returns->isVoid()) {
        body.push_back(StmtPtr(new ExprStmt(std::move(call))));
        body.push_back(StmtPtr(new Return(nullptr)));
    } else {
        body.push_back(StmtPtr(new Return(std::move(call))));
    }
    current_->functions.push_back(Function(name, f.returns, std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(name);
    current_->functions.back().setInline(true);
    closeFrame(savedFrame);
    return name;
}

void Parser::markSymbolUsed(const std::string &symbol) {
    if (symbol.empty()) return;
    for (std::size_t i = 0; i < functions_.size(); i++)
        if (functions_[i].symbol == symbol) {
            refuseDeleted(functions_[i], at_ + 1 < tokens_.size() ? peek().pos : functions_[i].pos);
            functions_[i].used = true;
            return;
        }
}

// A use of a deleted function is refused here as well as at a call: the compiler-written members reach their bases' and members' through this.
void Parser::markUsed(const Signature *f) {
    refuseDeleted(*f, at_ + 1 < tokens_.size() ? peek().pos : f->pos);
    functions_[static_cast<std::size_t>(f - &functions_[0])].used = true;
}

bool Parser::sameParameters(const std::vector<const Type *> &a,
                            const std::vector<const Type *> &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++)
        if (a[i] != b[i]) return false;
    return true;
}

bool Parser::memberFromBase(const Type *, const Member &m) {
    // Which class wrote it, not where it sits: a base's size can pass a vbase.
    return m.declaredIn != nullptr || m.inVirtualBase != nullptr;
}

// **[dcl.init]/7 refuses a const object that nothing would initialise**, and
// the line is CWG 253's rather than the paragraph's letter - which is what
// clang applies and what this was measured against.
bool Parser::constDefaultInitialisable(const Type *t) const {
    const Type *u = t->unqualified();
    while (u->isArray()) u = u->pointee()->unqualified();
    if (!u->isStructOrUnion()) return false;      // `const int n;` and its kind

    // A constructor somebody wrote initialises whatever it means to; one the
    // compiler wrote initialises only what the members ask for, which is the
    // whole of this question.
    if (const Signature *ctor = defaultConstructorOf(u))
        if (!ctor->implicit) return true;

    const std::vector<Type::BaseSpec> &bs = u->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (!constDefaultInitialisable(bs[i].type)) return false;

    const std::vector<Member> &ms = u->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        if (memberFromBase(u, ms[i])) continue;   // its own base answered for it
        if (memberInit_.count(u->tag() + "::" + ms[i].name)) continue;
        if (!constDefaultInitialisable(ms[i].type)) return false;
    }
    return true;
}

void Parser::requireConstInitialised(const Type *t, const std::string &name,
                                     std::size_t pos) {
    if (!t->isConst() || constDefaultInitialisable(t)) return;
    const Type *u = t->unqualified();
    while (u->isArray()) u = u->pointee()->unqualified();
    if (u->isStructOrUnion())
        // The suggestion is spelled with the tag and not with describe(), which
        // says "struct S" - and `struct S()` is not something anybody can write.
        src_.fail(pos, "'" + name + "' is const and nothing here would "
                       "initialise it - '" + u->describe() + "' has no "
                       "constructor of its own and leaves a member unset. Write "
                       "'const " + u->tag() + " " + name + " = " +
                       u->tag() + "();'");
    src_.fail(pos, "'" + name + "' is const and has no initialiser, so it would "
                   "hold whatever was there - [dcl.init]/7 refuses that. Give it "
                   "a value where it is declared");
}

// **A member of a virtual base is reached through the vtable**, because where
// that base sits depends on the complete object and not on the type written
// here.
void Parser::refuseVirtualBaseMember(const Type *staticType, const Member &m,
                                     const std::string &name,
                                     std::size_t pos) {
    if (m.inVirtualBase == nullptr || !target_.microsoftNames()) return;
    const Type *owner = staticType->unqualified();
    int baseAt = 0;
    if (owner->isStructOrUnion() && owner->vbptrOffset() >= 0 &&
        virtualBaseSlot(owner, m.inVirtualBase, &baseAt) > 0)
        return;                       // reachable through this class's vbtable
    src_.fail(pos, "'" + name + "' is a member of '" +
                   m.inVirtualBase->describe() + "', which is a virtual base "
                   "this class does not reach through a vbtable of its own, "
                   "and x86_64-windows has no other way there. Both Itanium "
                   "targets compile this");
}

// **The order a class's vbtable names its virtual bases in**: a vbptr shared
// with the base it came from is read by that base's code, so the base's order
// comes first, then the class's other virtual bases, in the order held.
static std::vector<const Type *> vbtableOrder(const Type *cls) {
    const Type *c = cls->unqualified();
    std::vector<const Type *> order;
    if (!c->vbptrs().empty() && c->vbptrs()[0].base != nullptr && c->vbptrs()[0].base != c)
        order = vbtableOrder(c->vbptrs()[0].base);
    for (const Type::BaseSpec &b : c->bases())
        if (b.isVirtual && std::find(order.begin(), order.end(), b.type) == order.end())
            order.push_back(b.type);
    return order;
}

int Parser::virtualBaseSlot(const Type *owner, const Type *vbase, int *baseAt) {
    const std::vector<const Type *> order = vbtableOrder(owner);
    for (std::size_t i = 0; i < order.size(); i++)
        if (order[i] == vbase) {
            if (baseAt != nullptr)
                for (const Type::BaseSpec &b : owner->bases())
                    if (b.isVirtual && b.type == vbase) *baseAt = b.offset;
            return static_cast<int>(i) + 1;
        }
    return 0;
}

// **The step from a derived object to its virtual base, on the Microsoft
// ABI**: `vbptrOffset + table[slot]`, where the table is the one the object's
// vbptr points at.
ExprPtr Parser::microsoftVirtualBaseStep(const Type *owner, const Type *vbase,
                                         const std::string &temp, int held,
                                         int *baseAt) const {
    const int vbp = owner->vbptrOffset();
    const int slot = virtualBaseSlot(owner, vbase, baseAt);
    if (vbp < 0 || slot <= 0) return ExprPtr();

    const Type *charPtr = types_.pointerTo(types_.get(Kind::Char));
    const Type *offType = types_.get(Kind::LongLong);
    const Type *entry = types_.intType();

    // `p = this + vbptrOffset`, the one address both the table and the base
    // are measured from.
    ExprPtr atVbptr(Var::local(temp, held));
    atVbptr->setType(charPtr);
    if (vbp != 0) {
        ExprPtr n(new Num(static_cast<long long>(vbp)));
        n->setType(offType);
        ExprPtr sum(new Binary(BinOp::Add, std::move(atVbptr), std::move(n)));
        sum->setType(charPtr);
        atVbptr = std::move(sum);
    }

    // `delta = ((int *)*(char **)p)[slot]`, four bytes and signed.
    ExprPtr asTable(new Cast(types_.pointerTo(charPtr), std::move(atVbptr)));
    asTable->setType(types_.pointerTo(charPtr));
    ExprPtr table(new Unary('*', std::move(asTable)));
    table->setType(charPtr);
    ExprPtr step(new Num(static_cast<long long>(slot) * 4));
    step->setType(offType);
    ExprPtr entryAt(new Binary(BinOp::Add, std::move(table), std::move(step)));
    entryAt->setType(charPtr);
    ExprPtr asEntry(new Cast(types_.pointerTo(entry), std::move(entryAt)));
    asEntry->setType(types_.pointerTo(entry));
    ExprPtr delta(new Unary('*', std::move(asEntry)));
    delta->setType(entry);
    ExprPtr wide(new Cast(offType, std::move(delta)));
    wide->setType(offType);
    if (vbp == 0) return wide;

    // The pointer stepped to the vbptr first, so that much is part of the
    // answer: the entry is measured from there and not from the object.
    ExprPtr n(new Num(static_cast<long long>(vbp)));
    n->setType(offType);
    ExprPtr sum(new Binary(BinOp::Add, std::move(wide), std::move(n)));
    sum->setType(offType);
    return sum;
}

// **Reaching a virtual base's member on the Microsoft ABI.**
ExprPtr Parser::microsoftVirtualBaseMember(ExprPtr object, const Type *owner,
                                           const Member &m) {
    if (owner->vbptrOffset() < 0) return ExprPtr();     // refused before this

    const Type *charPtr = types_.pointerTo(types_.get(Kind::Char));
    const Type *offType = types_.get(Kind::LongLong);

    // The object's address, kept in a slot: the step below reads it twice and
    // evaluating the expression twice would run its side effects twice.
    ExprPtr addr(new Unary('&', std::move(object)));
    addr->setType(types_.pointerTo(owner));
    const int held = allocateFrameSlot(charPtr);
    const std::string temp = ".vb" + std::to_string(refTemps_++);
    ExprPtr keep(Var::local(temp, held));
    keep->setType(charPtr);
    ExprPtr asChar(new Cast(charPtr, std::move(addr)));
    asChar->setType(charPtr);
    ExprPtr save(new Assign(std::move(keep), std::move(asChar)));
    save->setType(charPtr);

    int baseAt = 0;
    ExprPtr step = microsoftVirtualBaseStep(owner, m.inVirtualBase, temp, held,
                                            &baseAt);
    if (!step) return ExprPtr();

    ExprPtr from(Var::local(temp, held));
    from->setType(charPtr);
    ExprPtr moved(new Binary(BinOp::Add, std::move(from), std::move(step)));
    moved->setType(charPtr);
    // And where the member sits inside that base, which is constant.
    if (m.offset != baseAt) {
        ExprPtr n(new Num(static_cast<long long>(m.offset - baseAt)));
        n->setType(offType);
        ExprPtr sum(new Binary(BinOp::Add, std::move(moved), std::move(n)));
        sum->setType(charPtr);
        moved = std::move(sum);
    }

    ExprPtr asT(new Cast(types_.pointerTo(m.type), std::move(moved)));
    asT->setType(types_.pointerTo(m.type));
    ExprPtr whole(new Comma(std::move(save), std::move(asT)));
    whole->setType(types_.pointerTo(m.type));
    ExprPtr deref(new Unary('*', std::move(whole)));
    deref->setType(m.type);
    return deref;
}

ExprPtr Parser::virtualBaseMember(ExprPtr object, const Type *staticType,
                                  const Member &m) {
    if (m.inVirtualBase == nullptr) return ExprPtr();
    const Type *owner = staticType->unqualified();
    if (!owner->isStructOrUnion()) return ExprPtr();
    if (target_.microsoftNames())
        return microsoftVirtualBaseMember(std::move(object), owner, m);

    const long long back = itaniumVbaseOffsetSlot(owner, m.inVirtualBase);
    const int baseAt = virtualBaseAt(owner, m.inVirtualBase);
    if (back == 0 || baseAt < 0) return ExprPtr();

    const Type *charPtr = types_.pointerTo(types_.get(Kind::Char));
    const Type *offType = ptrdiffType();

    ExprPtr addr(new Unary('&', std::move(object)));
    addr->setType(types_.pointerTo(owner));
    const int held = allocateFrameSlot(charPtr);
    const std::string temp = ".vb" + std::to_string(refTemps_++);
    ExprPtr keep(Var::local(temp, held));
    keep->setType(charPtr);
    ExprPtr asChar(new Cast(charPtr, std::move(addr)));
    asChar->setType(charPtr);
    ExprPtr save(new Assign(std::move(keep), std::move(asChar)));
    save->setType(charPtr);

    ExprPtr obj(Var::local(temp, held));
    obj->setType(charPtr);
    ExprPtr asTable(new Cast(types_.pointerTo(charPtr), std::move(obj)));
    asTable->setType(types_.pointerTo(charPtr));
    ExprPtr vptr(new Unary('*', std::move(asTable)));
    vptr->setType(charPtr);
    ExprPtr backNum(new Num(back));
    backNum->setType(offType);
    ExprPtr at(new Binary(BinOp::Add, std::move(vptr), std::move(backNum)));
    at->setType(charPtr);
    ExprPtr asOff(new Cast(types_.pointerTo(offType), std::move(at)));
    asOff->setType(types_.pointerTo(offType));
    ExprPtr delta(new Unary('*', std::move(asOff)));
    delta->setType(offType);

    ExprPtr from(Var::local(temp, held));
    from->setType(charPtr);
    ExprPtr moved(new Binary(BinOp::Add, std::move(from), std::move(delta)));
    moved->setType(charPtr);
    const int within = m.offset - baseAt;
    if (within != 0) {
        ExprPtr w(new Num(static_cast<long long>(within)));
        w->setType(offType);
        ExprPtr sum(new Binary(BinOp::Add, std::move(moved), std::move(w)));
        sum->setType(charPtr);
        moved = std::move(sum);
    }
    ExprPtr asT(new Cast(types_.pointerTo(m.type), std::move(moved)));
    asT->setType(types_.pointerTo(m.type));
    ExprPtr whole(new Comma(std::move(save), std::move(asT)));
    whole->setType(types_.pointerTo(m.type));
    ExprPtr deref(new Unary('*', std::move(whole)));
    deref->setType(m.type);
    return deref;
}

ExprPtr Parser::thisMember(int thisSlot, const Type *cls, const Member &m) {
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(types_.pointerTo(cls));
    ExprPtr obj(new Unary('*', std::move(me)));
    obj->setType(cls);
    ExprPtr acc(new MemberAccess(std::move(obj), m.name, m.offset,
                                 m.width, m.bitOffset));
    acc->setType(m.type);
    return acc;
}

// **The type_info beside a vtable, and the name string beside that.**
std::string Parser::typeInfoSymbolFor(const Type *t, std::size_t pos,
                                      std::string *why) {
    const Type *u = t->unqualified();
    std::string name;
    if (itaniumTypeInfoName(u, &name, why)) return name;
    if (u->isPointer()) return emitPointerTypeInfo(u, pos, why);
    // Before the class below: a pointer to a member function wears the shape of a struct.
    if (u->isMemberPointer() || u->isMemberFunctionPointer()) return emitMemberPointerTypeInfo(u, pos, why);
    if (u->isEnumeration()) return emitEnumTypeInfo(u, pos, why);

    if (u->isStructOrUnion() && !u->tag().empty()) {
        // More than one base wants `__vmi_class_type_info`, which is not built
        // - emitClassTypeInfo answers empty for it and says so here.
        const std::string ti = emitClassTypeInfo(u, u->tag(), pos);
        if (!ti.empty()) { why->clear(); return ti; }
        *why = "'" + u->describe() + "' has a base this compiler cannot "
               "describe a type_info for";
        return std::string();
    }
    return std::string();
}

// **An enumeration's type_info is an `__enum_type_info`** - [ABI 2.9.5]:
// the vptr and the name string, nothing more, emitted weak beside its use.
std::string Parser::emitEnumTypeInfo(const Type *e, std::size_t pos,
                                     std::string *why) {
    (void)pos;
    std::string spelt;
    if (!itaniumTypeSpelling(e, &spelt, why)) return std::string();
    const std::string ti = "_ZTI" + spelt;
    for (std::size_t i = 0; i < current_->globals.size(); i++)
        if (current_->globals[i].symbol == ti) return ti;
    const std::string ts = "_ZTS" + spelt;
    std::vector<GlobalPiece> letters;
    for (std::size_t i = 0; i <= spelt.size(); i++)
        letters.push_back(GlobalPiece{ static_cast<int>(i), 1,
                                       i < spelt.size() ? spelt[i] : 0,
                                       std::string() });
    const Type *chars = types_.arrayOf(types_.get(Kind::Char),
                                       static_cast<long long>(spelt.size() + 1));
    current_->globals.push_back(Global{ ts, ts, chars, std::move(letters),
                                        true, false, true, std::string(), true });
    const int w = pointerBytes();
    std::vector<GlobalPiece> pieces;
    pieces.push_back(GlobalPiece{ 0, w, 2 * w, "_ZTVN10__cxxabiv116__enum_type_infoE" });
    pieces.push_back(GlobalPiece{ w, w, 0, ts });
    const Type *word = types_.pointerTo(types_.get(Kind::Void));
    const Type *object = types_.arrayOf(word, 2);
    current_->globals.push_back(Global{ ti, ti, object, std::move(pieces),
                                        true, false, true, std::string(), true });
    return ti;
}

// **A function's type_info is a `__function_type_info`** - [ABI 2.9.5]: the vptr and the name,
// emitted weak beside the pointer's that needs it; `typeid(int (*)(int))` and std::function's target.
std::string Parser::emitFunctionTypeInfo(const Type *fn, std::string *why) {
    std::string fspelt;
    if (!itaniumTypeSpelling(fn, &fspelt, why)) return std::string();
    const std::string inner = "_ZTI" + fspelt;
    for (std::size_t i = 0; i < current_->globals.size(); i++)
        if (current_->globals[i].symbol == inner) return inner;
    const std::string fts = "_ZTS" + fspelt;
    std::vector<GlobalPiece> fletters;
    for (std::size_t i = 0; i <= fspelt.size(); i++)
        fletters.push_back(GlobalPiece{ static_cast<int>(i), 1,
                                        i < fspelt.size() ? fspelt[i] : 0, std::string() });
    const Type *fchars = types_.arrayOf(types_.get(Kind::Char),
                                        static_cast<long long>(fspelt.size() + 1));
    current_->globals.push_back(Global{ fts, fts, fchars, std::move(fletters),
                                        true, false, true, std::string(), true });
    const int fw = pointerBytes();
    std::vector<GlobalPiece> fpieces;
    fpieces.push_back(GlobalPiece{ 0, fw, 2 * fw, "_ZTVN10__cxxabiv120__function_type_infoE" });
    fpieces.push_back(GlobalPiece{ fw, fw, 0, fts });
    const Type *fword = types_.pointerTo(types_.get(Kind::Void));
    current_->globals.push_back(Global{ inner, inner, types_.arrayOf(fword, 2), std::move(fpieces),
                                        true, false, true, std::string(), true });
    return inner;
}

// **A pointer to member's is a `__pointer_to_member_type_info`** - [ABI 2.9.5]: what a pointer's holds and then the
// class. The pointee of a pointer to a const member function is the `KFvvE` the function's own type spells.
std::string Parser::emitMemberPointerTypeInfo(const Type *ptr, std::size_t pos, std::string *why) {
    const Type *plain = ptr->unqualified();
    std::string spelt;
    if (!itaniumTypeSpelling(plain, &spelt, why)) return std::string();
    const std::string ti = "_ZTI" + spelt;
    for (std::size_t i = 0; i < current_->globals.size(); i++)
        if (current_->globals[i].symbol == ti) return ti;
    const Type *member = plain->pointee();
    const std::string inner = member->isFunction() ? emitFunctionTypeInfo(member, why)
                                                   : typeInfoSymbolFor(member, pos, why);
    if (inner.empty()) return std::string();
    const std::string context = typeInfoSymbolFor(plain->enclosing(), pos, why);
    if (context.empty()) return std::string();
    const std::string ts = "_ZTS" + spelt;
    std::vector<GlobalPiece> letters;
    for (std::size_t i = 0; i <= spelt.size(); i++)
        letters.push_back(GlobalPiece{ static_cast<int>(i), 1, i < spelt.size() ? spelt[i] : 0, std::string() });
    const Type *chars = types_.arrayOf(types_.get(Kind::Char), static_cast<long long>(spelt.size() + 1));
    current_->globals.push_back(Global{ ts, ts, chars, std::move(letters), true, false, true, std::string(), true });
    const int w = pointerBytes();
    std::vector<GlobalPiece> pieces;
    pieces.push_back(GlobalPiece{ 0, w, 2 * w, "_ZTVN10__cxxabiv129__pointer_to_member_type_infoE" });
    pieces.push_back(GlobalPiece{ w, w, 0, ts });
    pieces.push_back(GlobalPiece{ 2 * w, 4, (member->isConst() ? 1 : 0) | (member->isVolatile() ? 2 : 0), std::string() });
    pieces.push_back(GlobalPiece{ 3 * w, w, 0, inner });
    pieces.push_back(GlobalPiece{ 4 * w, w, 0, context });
    const Type *word = types_.pointerTo(types_.get(Kind::Void));
    current_->globals.push_back(Global{ ti, ti, types_.arrayOf(word, 5), std::move(pieces), true, false, true, std::string(), true });
    return ti;
}

// **A pointer's type_info is a `__pointer_type_info`** - [ABI 2.9.5]: vptr,
// name, the pointee's cv-qualifiers as flags, the pointee's own type_info.
// One to a fundamental type - `_ZTIPi`, `_ZTIPKc` - is the library's.
std::string Parser::emitPointerTypeInfo(const Type *ptr, std::size_t pos,
                                        std::string *why) {
    const Type *pointee = ptr->pointee();
    const Type *plain = pointee->unqualified();
    std::string spelt;
    if (!itaniumTypeSpelling(ptr, &spelt, why)) return std::string();
    // The library has a fundamental type's, its pointer's and its const pointer's; a volatile pointee's is made here.
    const bool builtin = itaniumBuiltinCode(plain->kind()) != nullptr && !pointee->isVolatile();
    if (builtin) return "_ZTI" + spelt;                   // the library's

    const std::string ti = "_ZTI" + spelt;
    for (std::size_t i = 0; i < current_->globals.size(); i++)
        if (current_->globals[i].symbol == ti) return ti;
    std::string inner;
    inner = plain->isFunction() ? emitFunctionTypeInfo(plain, why) : typeInfoSymbolFor(plain, pos, why);
    if (inner.empty()) return std::string();

    const std::string ts = "_ZTS" + spelt;
    std::vector<GlobalPiece> letters;
    for (std::size_t i = 0; i <= spelt.size(); i++)
        letters.push_back(GlobalPiece{ static_cast<int>(i), 1,
                                       i < spelt.size() ? spelt[i] : 0,
                                       std::string() });
    const Type *chars = types_.arrayOf(types_.get(Kind::Char),
                                       static_cast<long long>(spelt.size() + 1));
    current_->globals.push_back(Global{ ts, ts, chars, std::move(letters),
                                        true, false, true, std::string(), true });

    // vptr, name, flags (const 1, volatile 2), the pointee's type_info at the next pointer boundary.
    const int w = pointerBytes();
    std::vector<GlobalPiece> pieces;
    pieces.push_back(GlobalPiece{ 0, w, 2 * w, "_ZTVN10__cxxabiv119__pointer_type_infoE" });
    pieces.push_back(GlobalPiece{ w, w, 0, ts });
    pieces.push_back(GlobalPiece{ 2 * w, 4, (pointee->isConst() ? 1 : 0) | (pointee->isVolatile() ? 2 : 0),
                                  std::string() });
    pieces.push_back(GlobalPiece{ 3 * w, w, 0, inner });
    const Type *word = types_.pointerTo(types_.get(Kind::Void));
    const Type *object = types_.arrayOf(word, 4);
    current_->globals.push_back(Global{ ti, ti, object, std::move(pieces),
                                        true, false, true, std::string(), true });
    return ti;
}

// **The flags word of a `__vmi_class_type_info`**, [ABI 2.9.5]: bit 0 says a
// base class appears more than once but not diamond-shaped, bit 1 says it
// does.
long long Parser::itaniumVmiFlags(const Type *cls) {
    std::vector<const Type *> seen;
    std::vector<int> count;
    std::vector<bool> virtualPath;
    std::vector<const Type *> stack;
    std::vector<bool> stackVirtual;
    const std::vector<Type::BaseSpec> &direct = cls->bases();
    for (std::size_t i = 0; i < direct.size(); i++) {
        stack.push_back(direct[i].type);
        stackVirtual.push_back(direct[i].isVirtual);
    }
    while (!stack.empty()) {
        const Type *b = stack.back();
        const bool wasVirtual = stackVirtual.back();
        stack.pop_back();
        stackVirtual.pop_back();
        std::size_t k = 0;
        while (k < seen.size() && seen[k] != b) k++;
        if (k == seen.size()) {
            seen.push_back(b);
            count.push_back(0);
            virtualPath.push_back(false);
        }
        count[k]++;
        if (wasVirtual) virtualPath[k] = true;
        const std::vector<Type::BaseSpec> &up = b->bases();
        for (std::size_t i = 0; i < up.size(); i++) {
            stack.push_back(up[i].type);
            stackVirtual.push_back(wasVirtual || up[i].isVirtual);
        }
    }
    long long flags = 0;
    for (std::size_t k = 0; k < seen.size(); k++) {
        if (count[k] < 2) continue;
        flags |= virtualPath[k] ? 2 : 1;
    }
    return flags;
}

std::string Parser::emitClassTypeInfo(const Type *cls, const std::string &tag,
                                      std::size_t pos) {
    const std::string ti = cls->unqualified()->tag() == tag
                         ? itaniumClassTypeInfoSymbol(cls)
                         : itaniumClassTypeInfoSymbol(tag);
    for (std::size_t i = 0; i < current_->globals.size(); i++)
        if (current_->globals[i].symbol == ti) return ti;      // one per class
    classSymbols_[tag].push_back(ti);

    // **Which of the three shapes this class is.**
    std::vector<Type::BaseSpec> bases;
    {
        const std::vector<Type::BaseSpec> &all = cls->bases();
        for (std::size_t i = 0; i < all.size(); i++)
            if (all[i].direct) bases.push_back(all[i]);
    }
    const bool simple = bases.size() == 1 && !bases[0].isVirtual &&
                        bases[0].offset == 0 &&
                        bases[0].access == Access::Public;

    // **Every base first, and nothing is laid down until they all answer.**
    std::vector<std::string> baseTypeInfo;
    for (std::size_t i = 0; i < bases.size(); i++) {
        const std::string one = emitClassTypeInfo(bases[i].type,
                                                  bases[i].type->tag(), pos);
        if (one.empty()) return std::string();
        baseTypeInfo.push_back(one);
    }

    const bool own = cls->unqualified()->tag() == tag;
    const std::string ts = own ? itaniumClassTypeNameSymbol(cls)
                               : itaniumClassTypeNameSymbol(tag);
    classSymbols_[tag].push_back(ts);
    const std::string text = own ? itaniumClassNameString(cls)
                                 : itaniumClassNameString(tag);
    std::vector<GlobalPiece> letters;
    for (std::size_t i = 0; i <= text.size(); i++)             // the NUL too
        letters.push_back(GlobalPiece{ static_cast<int>(i), 1,
                                       i < text.size() ? text[i] : 0,
                                       std::string() });
    const Type *chars = types_.arrayOf(types_.get(Kind::Char),
                                       static_cast<long long>(text.size() + 1));
    // **Weak**: every translation unit that sees the class emits these three,
    // and the linker folds them rather than rejecting them - which is what
    // clang does and what a program of more than one file needs.
    current_->globals.push_back(Global{ ts, ts, chars, std::move(letters),
                                        true, false, true,
                                        std::string(), true });

    // Every field is a pointer or pointer-wide, except the two `unsigned int`
    // of a vmi class, so the layout is written in pointer widths: w.
    const int w = pointerBytes();
    std::vector<GlobalPiece> pieces;
    pieces.push_back(GlobalPiece{
        0, w, 2 * w,
        bases.empty() ? "_ZTVN10__cxxabiv117__class_type_infoE"
                      : simple ? "_ZTVN10__cxxabiv120__si_class_type_infoE"
                               : "_ZTVN10__cxxabiv121__vmi_class_type_infoE" });
    pieces.push_back(GlobalPiece{ w, w, 0, ts });
    int bytes = 2 * w;
    if (simple) {
        pieces.push_back(GlobalPiece{ 2 * w, w, 0, baseTypeInfo[0] });
        bytes = 3 * w;
    } else if (!bases.empty()) {
        // Two `unsigned int` after the header - the class's flags and the
        // number of bases - and then a pair of words per base: its `_ZTI` and
        // an offset with four flag bits under it.
        pieces.push_back(GlobalPiece{ 2 * w, 4, itaniumVmiFlags(cls),
                                      std::string() });
        pieces.push_back(GlobalPiece{ 2 * w + 4, 4,
                                      static_cast<long long>(bases.size()),
                                      std::string() });
        int at = 2 * w + 8;
        for (std::size_t i = 0; i < bases.size(); i++) {
            pieces.push_back(GlobalPiece{ at, w, 0, baseTypeInfo[i] });
            // **`__public_mask` is 2 and `__virtual_mask` is 1**, and the offset above them is
            // where the base *is* - except for a virtual one, where it is where its `vbase_offset`
            // sits in the vtable, a negative number the runtime reads through the object's vptr.
            long long flags = bases[i].access == Access::Public ? 2 : 0;
            long long where = bases[i].offset;
            if (bases[i].isVirtual) {
                flags |= 1;
                where = itaniumVbaseOffsetSlot(cls, bases[i].type);
            }
            pieces.push_back(GlobalPiece{ at + w, w, (where << 8) | flags,
                                          std::string() });
            at += 2 * w;
        }
        bytes = at;
    }

    const Type *word = types_.pointerTo(types_.get(Kind::Void));
    const Type *object = types_.arrayOf(word, (bytes + w - 1) / w);
    current_->globals.push_back(Global{ ti, ti, object, std::move(pieces),
                                        true, false, true,
                                        std::string(), true });
    return ti;
}

// **The Microsoft vbtable**, which has no Itanium counterpart: that ABI keeps
// a virtual base's offset in the vftable and this one keeps it in a table of
// its own, reached through a second pointer.
void Parser::emitVbtable(const Type *cls, const std::string &tag,
                         std::size_t pos) {
    if (!target_.microsoftNames()) return;
    const Type *plain = cls->unqualified();
    const std::vector<Type::VbPtr> &ptrs = plain->vbptrs();
    (void)pos;

    // **One table per vbptr, and the name says which.**
    for (std::size_t p = 0; p < ptrs.size(); p++) {
        const int vbp = ptrs[p].offset;
        const std::string symbol = vbptrTable(tag, ptrs, p);
        bool already = false;
        for (std::size_t i = 0; i < current_->globals.size(); i++)
            if (current_->globals[i].symbol == symbol) already = true;
        if (already) continue;                              // one per class

        // **Entry 0 steps back to the top of the class that introduced the
        // pointer**, not to the top of this one. cl.
        const Type *owner = ptrs[p].owner != nullptr ? ptrs[p].owner : plain;
        std::vector<GlobalPiece> pieces;
        int at = 0;
        pieces.push_back(GlobalPiece{ at, 4,
                                      -static_cast<long long>(
                                          owner->vbptrOffset()),
                                      std::string() });
        at += 4;

        // **Which bases the table names, and in whose order**: the order code reading through this
        // pointer numbers them by - this class's for its first pointer, the holder's for the rest.
        const std::vector<Type::BaseSpec> &bs = plain->bases();
        const std::vector<const Type *> want =
            p != 0 && ptrs[p].base != nullptr ? vbtableOrder(ptrs[p].base) : vbtableOrder(plain);

        for (std::size_t i = 0; i < want.size(); i++) {
            // Where that base sits **in this object**, which is the whole
            // point of the class writing a table of its own.
            int where = -1;
            for (std::size_t j = 0; j < bs.size(); j++)
                if (bs[j].isVirtual && bs[j].type == want[i])
                    where = bs[j].offset;
            if (where < 0) continue;
            pieces.push_back(GlobalPiece{ at, 4,
                static_cast<long long>(where - vbp), std::string() });
            at += 4;
        }

        const Type *entry = types_.intType();
        const Type *table = types_.arrayOf(entry,
                                           static_cast<long long>(
                                               pieces.size()));
        current_->globals.push_back(Global{ symbol, symbol, table,
                                            std::move(pieces), true, false,
                                            true, std::string(), true });
    }
}

// **A base's tables come down at its offset**, overridden slots replaced; the
// first base's first is the class's own (vtables_[tag]). Names follow clang's
// computeVTablePaths, paths spelled alike extended until none is.
std::vector<Parser::MsVftable> Parser::msVftables(const Type *cls) {
    const Type *plain = cls->unqualified();
    const std::string tag = plain->tag();
    std::vector<MsVftable> out;
    const std::vector<Type::BaseSpec> &bs = plain->bases();
    for (std::size_t bi = 0; bi < bs.size(); bi++) {
        if (bs[bi].isVirtual || !bs[bi].type->polymorphic()) continue;
        const Type *b = bs[bi].type->unqualified();
        std::vector<MsVftable> theirs = msVftables(b);
        for (MsVftable &t : theirs) {
            if (t.vbase != nullptr) continue;   // its virtual bases are this class's, met below
            t.at += bs[bi].offset;
            for (int &a : t.thisAt) a += bs[bi].offset;
            if (t.path.empty() || t.path.back() != b) t.nextToMangle = b;
            for (std::size_t i = 0; i < t.slots.size(); i++) {
                if (t.slots[i].name == "~") {
                    for (const VSlot &own : vtables_[tag])
                        if (own.name == "~") { t.slots[i].symbol = own.symbol; t.slots[i].pure = own.pure; t.thisAt[i] = 0; }
                    continue;
                }
                const std::vector<std::size_t> *set = overloadsOf(tag + "::" + t.slots[i].name);
                if (set == nullptr) continue;
                for (std::size_t k = 0; k < set->size(); k++) {
                    const Signature &f = functions_[(*set)[k]];
                    if (f.owner != tag || !f.isVirtual) continue;
                    if (!overrides(t.slots[i], f.name, f.params, f.constThis, f.refThis)) continue;
                    const bool pure = msPure_.count(f.symbol) != 0;
                    t.slots[i].symbol = pure ? pureVirtualSymbol()
                                      : deletedVirtuals_.count(f.symbol) ? deletedVirtualSymbol() : f.symbol;
                    t.slots[i].pure = pure;
                    t.thisAt[i] = msThisAdjust(f.symbol);
                }
            }
            out.push_back(t);
        }
    }
    if (out.empty() && !vtables_[tag].empty()) out.push_back(MsVftable());
    if (!out.empty()) {
        out[0].slots = vtables_[tag];
        out[0].thisAt.assign(out[0].slots.size(), 0);
    }
    // **A virtual base's tables once each, where the complete object holds it** (M3): every entry the
    // final overrider below this class, where the base keeps its own function if nothing overrides it.
    for (std::size_t bi = 0; bi < bs.size(); bi++) {
        if (!bs[bi].isVirtual || !bs[bi].type->polymorphic()) continue;
        const Type *v = bs[bi].type->unqualified();
        std::vector<MsVftable> theirs = msVftables(v);
        for (MsVftable &t : theirs) {
            if (t.vbase != nullptr) continue;
            t.vbase = v;
            t.atInVbase = t.at;
            t.vtordisp = msVtordisp_.count(tag) != 0 && msVtordisp_[tag].count(v) != 0;
            t.below.assign(t.slots.size(), 0);
            t.at += bs[bi].offset;
            for (int &a : t.thisAt) a += bs[bi].offset;
            if (t.path.empty() || t.path.back() != v) t.nextToMangle = v;
            for (std::size_t i = 0; i < t.slots.size(); i++) {
                std::string symbol;
                int at = 0;
                bool pure = false;
                if (!msFinalOverrider(plain, plain, 0, v, t.slots[i], &symbol, &at, &pure)) continue;
                t.below[i] = 1;
                t.slots[i].symbol = symbol;
                t.slots[i].pure = pure;
                t.thisAt[i] = at;
            }
            out.push_back(t);
        }
    }
    for (bool changed = true; changed; ) {
        changed = false;
        for (std::size_t i = 0; i < out.size(); i++) {
            bool alike = false;
            for (std::size_t j = 0; j < out.size(); j++)
                alike = alike || (j != i && out[j].path == out[i].path);
            if (!alike) continue;
            for (std::size_t j = i; j < out.size(); j++) {
                if (out[j].path != out[i].path || out[j].nextToMangle == nullptr) continue;
                out[j].path.push_back(out[j].nextToMangle);
                out[j].nextToMangle = nullptr;
                changed = true;
            }
        }
    }
    return out;
}

bool Parser::msFinalOverrider(const Type *root, const Type *k, int kAt, const Type *vbase,
                              const VSlot &slot, std::string *symbol, int *thisAt, bool *pure) {
    const std::string tag = k->tag();
    if (slot.name == "~") {
        const Signature *dt = destructorOf(k);
        if (dt == nullptr || !dt->isVirtual) return false;
        *symbol = deletingDestructorSymbol(tag);
        *thisAt = kAt + msThisAdjust(*symbol);
        *pure = false;
        return true;
    }
    if (const std::vector<std::size_t> *set = overloadsOf(tag + "::" + slot.name))
        for (std::size_t i = 0; i < set->size(); i++) {
            const Signature &f = functions_[(*set)[i]];
            if (f.owner != tag || !f.isVirtual) continue;
            if (!overrides(slot, f.name, f.params, f.constThis, f.refThis)) continue;
            *pure = msPure_.count(f.symbol) != 0;
            *symbol = *pure ? pureVirtualSymbol()
                    : deletedVirtuals_.count(f.symbol) ? deletedVirtualSymbol() : f.symbol;
            *thisAt = kAt + msThisAdjust(f.symbol);
            return true;
        }
    for (const Type::BaseSpec &b : k->bases()) {
        const Type *bt = b.type->unqualified();
        if (!b.direct || bt == vbase || !msHasBase(bt, vbase)) continue;
        int bAt = kAt + b.offset;
        // A virtual base is where the complete object holds it, not where `k` alone would.
        if (b.isVirtual)
            for (const Type::BaseSpec &r : root->bases())
                if (r.isVirtual && r.type->unqualified() == bt) bAt = r.offset;
        if (msFinalOverrider(root, bt, bAt, vbase, slot, symbol, thisAt, pure)) return true;
    }
    return false;
}

// One entered at a virtual base is resolved when asked, its class laid by then: an implicit destructor
// is declared only at the class's end, after the members' were.
int Parser::msThisAdjust(const std::string &symbol) const {
    std::map<std::string, int>::const_iterator it = msThisAdjust_.find(symbol);
    if (it != msThisAdjust_.end()) return it->second;
    std::map<std::string, MsVbaseAdjust>::const_iterator v = msVbaseAdjust_.find(symbol);
    if (v == msVbaseAdjust_.end()) return 0;
    if (const Type *cls = findTypedef(v->second.cls))
        for (const Type::BaseSpec &b : cls->unqualified()->bases())
            if (b.isVirtual && b.type->unqualified() == v->second.vbase) return b.offset + v->second.within;
    return 0;
}

bool Parser::msHasBase(const Type *d, const Type *b) {
    for (const Type::BaseSpec &s : d->unqualified()->bases())
        if (s.type->unqualified() == b || msHasBase(s.type, b)) return true;
    return false;
}

void Parser::msEnterAdjusted(std::vector<StmtPtr> &body, int thisSlot, const Type *self,
                             const std::string &symbol) {
    const int adjust = msThisAdjust(symbol);
    if (!target_.microsoftNames() || adjust == 0) return;
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    ExprPtr to(Var::local("this", thisSlot));
    to->setType(self);
    ExprPtr set(new Assign(std::move(to), msShifted(std::move(me), -adjust)));
    set->setType(self);
    body.insert(body.begin(), StmtPtr(new ExprStmt(std::move(set))));
}

bool Parser::msVfptrInVirtualBase(const Type *cls) {
    const std::vector<MsVftable> tables = msVftables(cls);
    return !tables.empty() && tables[0].vbase != nullptr;
}

ExprPtr Parser::msToVfptr(ExprPtr p, const Type *cls) {
    const std::vector<MsVftable> tables = msVftables(cls);
    if (tables.empty() || tables[0].vbase == nullptr) return p;
    const Type *t = p->type();
    ExprPtr there(new Cast(t, msShifted(convert(std::move(p), types_.pointerTo(tables[0].vbase)),
                                        tables[0].atInVbase)));
    there->setType(t);
    return there;
}

ExprPtr Parser::msShifted(ExprPtr self, int by) {
    if (by == 0) return self;
    const Type *t = self->type();
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    ExprPtr asChars(new Cast(chars, std::move(self)));
    asChars->setType(chars);
    ExprPtr n(new Num(static_cast<long long>(by)));
    n->setType(types_.intType());
    ExprPtr moved(new Binary(BinOp::Add, std::move(asChars), std::move(n)));
    moved->setType(chars);
    ExprPtr back(new Cast(t, std::move(moved)));
    back->setType(t);
    return back;
}

// clang's MSRTTIBuilder again, with virtual bases: a virtual base's row reads it through the complete
// class's vbptr - (0, vbptr, 4 * its index, 0x50) - and the bases inside it the same, at their offset
// in it. A virtual base met twice is one subobject listed twice; a repeated non-virtual one refuses.
bool Parser::msRttiRows(const Type *cls, std::vector<MicrosoftBaseClass> *rows, int *attributes) {
    const Type *plain = cls->unqualified();
    rows->clear();
    *attributes = 0;
    bool virtualBases = false;
    struct Walk {
        Parser *p;
        const Type *root;
        std::vector<MicrosoftBaseClass> *rows;
        bool *virtualBases;
        void at(const Type *k, int mdisp, int pdisp, int vdisp, int attr, bool hidden) {
            const std::size_t self = rows->size();
            MicrosoftBaseClass row;
            row.cls = k;
            row.mdisp = mdisp;
            row.attributes = attr;
            row.pdisp = pdisp;
            row.vdisp = vdisp;
            rows->push_back(row);
            for (const Type::BaseSpec &b : k->bases()) {
                if (!b.direct) continue;
                const bool edge = b.access != Access::Public;
                const bool under = hidden || edge;
                int a = 0x40 | (under ? 0x09 : 0) | (edge ? 0x04 : 0);
                if (b.isVirtual) {
                    *virtualBases = true;
                    const int slot = Parser::virtualBaseSlot(root, b.type->unqualified(), nullptr);
                    at(b.type->unqualified(), 0, root->vbptrOffset(), 4 * slot, a | 0x10, under);
                } else {
                    at(b.type->unqualified(), mdisp + b.offset, pdisp, vdisp, a | (pdisp >= 0 ? 0x10 : 0), under);
                }
            }
            (*rows)[self].contained = static_cast<int>(rows->size() - self - 1);
        }
    } walk{ this, plain, rows, &virtualBases };
    walk.at(plain, 0, -1, 0, 0x40, false);
    for (std::size_t i = 0; i < rows->size(); i++) {
        MicrosoftBaseClass &r = (*rows)[i];
        int direct = 0;
        for (const Type::BaseSpec &b : r.cls->bases()) direct += b.direct ? 1 : 0;
        if (direct > 1) *attributes |= 1;
        for (std::size_t j = 0; j < i; j++)
            if ((*rows)[j].cls == r.cls && (r.attributes & 0x10) == 0) return false;
        MicrosoftRtti n;
        std::string why;
        if (!microsoftClassRttiNames(r.cls, &n, &why)) return false;
        r.name = microsoftBaseDescriptorName(r);
    }
    if ((*attributes & 1) != 0 && virtualBases) *attributes |= 2;
    return true;
}

// An adjustor thunk for one table entry: the function's own name with the adjustment in it, its body
// the same forwarding call the Itanium thunk makes.
std::string Parser::msAdjustorThunk(const std::string &cls, const Type *type, const VSlot &slot,
                                    int adjust, std::size_t pos, bool vtordisp) {
    std::string name, why;
    // The deleting destructor's slot holds `??_G`, which takes the flag and answers `this`.
    if (slot.name == "~") {
        bool own = slot.symbol == deletingDestructorSymbol(cls);
        for (const VSlot &s : vtables_[cls]) own = own || (s.name == "~" && s.symbol == slot.symbol);
        if (!own)
            src_.fail(pos, "'" + cls + "' inherits a destructor in a table after its first, which is "
                           "not supported yet for x86_64-windows");
        name = microsoftDeletingDestructorThunkName(cls, findTypedef(cls), adjust, vtordisp);
        VSlot deleting = slot;
        deleting.params.push_back(types_.get(Kind::UInt));
        return synthesizeThunk(cls, type, deleting, adjust, pos, name,
                               types_.pointerTo(types_.get(Kind::Void)), vtordisp);
    }
    std::map<std::string, MsVirtualDecl>::const_iterator d = msVirtualDecls_.find(slot.symbol);
    if (d == msVirtualDecls_.end() ||
        !microsoftAdjustorThunkName(d->second.cls, findTypedef(d->second.cls), d->second.name,
                                    manglingType(d->second.fn), d->second.constThis,
                                    d->second.refThis, adjust, &name, &why, vtordisp))
        src_.fail(pos, "the adjustor thunk for '" + cls + "::" + slot.name + "' cannot be named" +
                       (why.empty() ? std::string() : ": " + why));
    return synthesizeThunk(d->second.cls, findTypedef(d->second.cls), slot, adjust, pos, name, nullptr,
                           vtordisp);
}

void Parser::emitVtable(const Type *cls, const std::string &tag,
                        std::size_t pos) {
    if (tag.empty())
        src_.fail(pos, "a class with a virtual function needs a name - its "
                       "vtable is a symbol, and an anonymous class has none");

    const std::vector<VSlot> &slots = vtables_[tag];
    const bool ms = target_.microsoftNames();
    const std::string symbol = cls->unqualified()->tag() == tag
                             ? vtableSymbol(cls, ms) : vtableSymbol(tag, ms);

    for (std::size_t i = 0; i < current_->globals.size(); i++)
        if (current_->globals[i].symbol == symbol) return;   // one per class
    classSymbols_[tag].push_back(symbol);

    // **The table holding a function's address is a use of it.** The `used` flag came
    // only from calls, so a class with an implicit virtual destructor got a table
    // pointing at a `~D` nothing emitted. Marked during the class's own completion.
    for (std::size_t i = 0; i < slots.size(); i++)
        markSymbolUsed(slots[i].symbol);
    // **And the destructor itself, which the Microsoft table does not name.**
    if (const Signature *dtor = destructorOf(cls)) markSymbolUsed(dtor->symbol);

    // **The Itanium group is its own file's work** - ParserVtable.cpp - and
    // the Microsoft vftable is the one table below.
    if (!ms) {
        emitItaniumVtables(cls, tag, symbol, pos);
        return;
    }

    // **A table per vfptr** (Phase 6): `??_7X@@6BI1@@@` and `??_7X@@6BI2@@@`, or `??_7X@@6B@` alone. An
    // entry whose function wants `this` elsewhere than this vfptr goes through an adjustor thunk.
    const std::vector<MsVftable> tables = msVftables(cls);
    const std::string prefix = symbol.substr(0, symbol.size() - 3);
    if (!tables.empty() && tables[0].path.size() > 0)
        for (std::size_t i = 0; i < current_->globals.size(); i++)
            if (current_->globals[i].symbol == prefix + "6B" + microsoftVftablePath(tables[0].path) + "@") return;
    MicrosoftRtti names;
    std::string why;
    std::vector<MicrosoftBaseClass> rows;
    int hierarchy = 0;
    // **The Microsoft locator goes in front of the table, not behind it**, one per table.
    const bool described = microsoftClassRttiNames(cls, &names, &why) && msRttiRows(cls, &rows, &hierarchy);
    if (described) {
        bool had = false;
        for (std::size_t i = 0; i < current_->rtti.size(); i++)
            if (current_->rtti[i] == cls) had = true;
        if (!had) {
            current_->rtti.push_back(cls);
            current_->msRtti.push_back(Program::MsRtti{ cls, hierarchy, rows });
        }
    }
    for (std::size_t t = 0; t < tables.size(); t++) {
        const std::string path = microsoftVftablePath(tables[t].path);
        const std::string name = prefix + "6B" + path + "@";
        std::vector<GlobalPiece> pieces;
        const int w = pointerBytes();                  // one entry
        for (std::size_t i = 0; i < tables[t].slots.size(); i++) {
            std::string entrySym = tables[t].slots[i].symbol;
            const bool viaVtordisp = tables[t].vtordisp && tables[t].below[i] != 0;
            if ((viaVtordisp || tables[t].thisAt[i] != tables[t].at) && !tables[t].slots[i].pure)
                entrySym = msAdjustorThunk(tag, cls, tables[t].slots[i],
                                           tables[t].at - tables[t].thisAt[i], pos, viaVtordisp);
            markSymbolUsed(entrySym);
            pieces.push_back(GlobalPiece{ static_cast<int>(i) * w, w, 0, entrySym });
        }
        std::string locatorWord;
        if (described) {
            locatorWord = names.locator.substr(0, names.locator.size() - 3) + "6B" + path + "@";
            current_->msLocators.push_back(Program::MsLocator{ cls, locatorWord, tables[t].at,
                                                               tables[t].vtordisp ? 4 : 0 });
        }
        const Type *entry = types_.pointerTo(types_.get(Kind::Void));
        const Type *table = types_.arrayOf(entry, static_cast<long long>(pieces.size()));
        current_->globals.push_back(Global{ name, name, table, std::move(pieces),
                                            true, false, true, locatorWord, true });
    }
}

// A constructor, read at the point its '(' was seen: a member function whose name is
// the class and whose return type is nothing at all, keyed under "Point::Point" so
// every piece of overload machinery applies to it unchanged.
void Parser::declareConstructor(const std::string &cls, std::size_t pos,
                                Access access, bool isExplicit) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    if (variadic)
        src_.fail(pos, "a constructor cannot take '...'");
    refuseSpecialRefQualifier("constructor");
    // [class.copy]/6: a constructor whose first parameter is the class itself, and the rest defaulted,
    // would be called to copy its own argument. A constructor template is never made into one.
    if (!memberTemplateInst_ && !params.empty() && params[0]->unqualified()->isStructOrUnion() &&
        findTypedef(cls) != nullptr &&
        params[0]->unqualified() == findTypedef(cls)->unqualified()) {
        bool restDefaulted = true;
        for (std::size_t i = 1; i < params.size(); i++)
            restDefaulted = restDefaulted && i < pendingDefaults_.size() && pendingDefaults_[i] != 0;
        if (restDefaulted)
            src_.fail(pos, "a constructor of '" + cls + "' cannot take a '" + params[0]->describe() + "' by "
                           "value - copying the argument would call it again; take 'const " +
                           params[0]->unqualified()->describe() + " &' - [class.copy]/6");
    }
    // Read here rather than at the call site.
    pendingNoexcept_ = exceptionSpecification();
    const SpecialDef special = takeSpecialDef();

    // **A defaulted constructor is the implicit one and is not declared here**: declareImplicitSpecials
    // writes it exactly as it would have, and reads `defaulted_` for the access and for [class.copy]/9.
    if (special == SpecialDef::Defaulted) {
        const Type *self = findTypedef(cls);
        Defaulted &df = defaulted_[cls];
        const Type *p = params.size() == 1 ? params[0] : nullptr;
        const bool ofSelf = p != nullptr && p->isReference() && self != nullptr &&
                            p->referent()->unqualified() == self->unqualified();
        if (params.empty())                      defaultedMember(df.defaultCtor, access, pos, "the default constructor");
        else if (ofSelf && !p->isRValueReference()) defaultedMember(df.copyCtor, access, pos, "the copy constructor");
        else if (ofSelf)                         defaultedMember(df.moveCtor, access, pos, "the move constructor");
        else
            src_.fail(pendingSpecialAt_, "'" + cls + "::" + cls + "' with these parameters is not a special "
                                         "member function, and only one of those may be defaulted - "
                                         "[dcl.fct.def.default]/1");
        return;
    }

    // A constructor returns nothing, and saying so as void is what lets the rest of the compiler treat the call like any other.
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);

    std::string key = constructorKey(cls);
    std::vector<std::size_t> &set = functionIndex_[key];
    for (std::size_t k = 0; k < set.size(); k++) {
        const Signature &f = functions_[set[k]];
        if (sameParameters(f.params, params))
            src_.fail(pos, "'" + cls + "::" + cls + "' is declared twice");
    }

    const char code = access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    std::string out, why;
    bool ok = target_.microsoftNames()
            ? microsoftConstructorName(cls, findTypedef(cls), manglingType(fn),
                                       code, &out, &why)
            : itaniumConstructorName(cls, findTypedef(cls), fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + cls + "::" + cls + "' cannot be given a name the "
                       "linker can hold: " + why);

    set.push_back(functions_.size());
    requireDefaultsAreASuffix(pendingDefaults_, pendingDefaultsAt_);
    if (!pendingDefaults_.empty()) defaultArgs_[out] = pendingDefaults_;
    if (!pendingDefaults_.empty() && !instantiatingParams_.empty())   // a class template's, read with its arguments (R17)
        defaultArgBindings_[out] = DefaultBinding{ instantiatingParams_, instantiatingBinding_, instantiatingValues_, Packs() };
    if (!pendingDefaults_.empty())
        defaultArgNamespace_[out] = namespaceStack_;
    pendingDefaults_.clear();
    functions_.push_back(Signature{ cls, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, cls, false, access });
    functions_.back().isExplicit = isExplicit;
    functions_.back().isNoexcept = pendingNoexcept_;
    keepDeferredSpec();
    functions_.back().spec = pendingSpec_;
    functions_.back().deleted = special == SpecialDef::Deleted;
    pendingNoexcept_ = false;
}

// **How many objects a member array holds, and of what type.** Every dimension
// counts: `E g[2][3]` is six elements of `E`, not two of `E[3]`, and
// [class.base.init]/8 default-initialises every one of them.
static const Type *memberElements(const Type *t, long long *count) {
    *count = 1;
    while (t != nullptr && t->isArray()) {
        if (t->length() < 0) { *count = -1; return t->pointee(); }
        *count *= t->length();
        t = t->pointee();
    }
    return t;
}

// The class a member is built from: the element type when the member is an
// array, and nothing at all when it is not of class type.
static const Type *memberClass(const Type *t) {
    while (t != nullptr && t->isArray()) t = t->pointee();
    return (t != nullptr && t->isStructOrUnion()) ? t->unqualified() : nullptr;
}

// [except.spec]/14: an implicit special member allows no exception when nothing it invokes allows one - each
// base's and member's own member of the kind (a move falling back on the copy), and for the default
// constructor each member's initialiser, taken as one that may throw.
bool Parser::implicitNoexcept(const Type *type, int kind) const {
    auto quiet = [&](const Type *c) -> bool {
        if (c == nullptr) return true;
        const Signature *s = nullptr;
        switch (kind) {
        case kImplicitDefault: s = defaultConstructorOf(c); break;
        case kImplicitCopy: s = copyConstructorOf(c); break;
        case kImplicitMove: s = moveConstructorOf(c); if (s == nullptr) s = copyConstructorOf(c); break;
        case kImplicitCopyAssign: s = copyAssignOf(c); break;
        default: s = moveAssignOf(c); if (s == nullptr) s = copyAssignOf(c); break;
        }
        return s == nullptr || s->isNoexcept;
    };
    for (const Type::BaseSpec &b : type->bases())
        if ((b.direct || b.isVirtual) && !quiet(b.type->unqualified())) return false;
    for (const Member &m : type->members())
        if (m.declaredIn == nullptr && m.inVirtualBase == nullptr && !quiet(memberClass(m.type))) return false;
    if (kind == kImplicitDefault)
        for (const MemberDefault &d : memberDefaults_)
            if (d.cls == type) return false;
    return true;
}

bool Parser::destructorMayThrow(const Type *cls) const {
    const Signature *dtor = cls == nullptr ? nullptr : destructorOf(cls);
    if (dtor == nullptr || dtor->isNoexcept) return false;
    if (dtor->spec.written || dtor->spec.allowsAll) return true;
    for (const Type::BaseSpec &b : cls->bases())
        if ((b.direct || b.isVirtual) && destructorMayThrow(b.type->unqualified())) return true;
    for (const Member &m : cls->members())
        if (m.declaredIn == nullptr && m.inVirtualBase == nullptr && destructorMayThrow(memberClass(m.type))) return true;
    return false;
}

// **[except.ctor]/2: a destructor that throws leaves the object's other subobjects to be destroyed.** Where one
// of this class's may, each step - `body` where there is one, then each member's and base's destructor - runs
// under a region that destroys the rest, in a block of its own so that the regions do not chain.
StmtPtr Parser::destroySubobjects(StmtPtr body, const Type *type, int thisSlot, std::size_t pos) {
    std::vector<Alive> left;
    const std::vector<Member> &ms = type->members();
    for (std::size_t n = ms.size(); type->kind() != Kind::Union && n-- > 0; )
        if (!memberFromBase(type, ms[n]) && memberHasDestructor(ms[n].type)) {
            left.push_back(Alive{ "this", thisSlot, type });
            left.back().owner = type;
            left.back().member = static_cast<int>(n);
        }
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t n = bs.size(); n-- > 0; )
        if (bs[n].direct && !bs[n].isVirtual && destructorOf(bs[n].type) != nullptr) {
            left.push_back(Alive{ "this", thisSlot, type });
            left.back().owner = type;
            left.back().base = static_cast<int>(n);
        }
    std::vector<StmtPtr> out;
    for (std::size_t k = 0; k <= left.size(); k++) {
        std::vector<StmtPtr> step;
        if (k > 0) destroyObject(step, left[k - 1], pos);
        else if (body != nullptr) step.push_back(std::move(body));
        if (step.empty()) continue;
        if (k == left.size()) {
            for (StmtPtr &s : step) out.push_back(std::move(s));
            break;
        }
        std::vector<StmtPtr> rest;
        for (std::size_t j = k; j < left.size(); j++) destroyObject(rest, left[j], pos);
        Block *b = new Block(std::move(rest));
        b->setScope(-1);
        std::vector<StmtPtr> region;
        region.push_back(cleanupRegion(std::move(step), StmtPtr(b)));
        Block *alone = new Block(std::move(region));
        alone->setScope(-1);
        out.push_back(StmtPtr(alone));
    }
    Block *all = new Block(std::move(out));
    all->setScope(-1);
    return StmtPtr(all);
}

bool Parser::memberHasDestructor(const Type *t) const {
    return destructorOf(memberClass(t)) != nullptr;
}

// One element of an array member, by address: the member's own address, decayed, plus
// the index times the element's size. **In bytes, and deliberately** - a Binary built
// here is not the parser's pointer arithmetic and gets none of its scaling.
static ExprPtr indexBytes(TypeTable &types, ExprPtr decayed, const Type *elem,
                          int indexSlot, const Target &target) {
    const Type *idx = types.intType();
    ExprPtr i(Var::local("$i", indexSlot));
    i->setType(idx);
    ExprPtr size(new Num(static_cast<long long>(elem->size(target))));
    size->setType(idx);
    ExprPtr off(new Binary(BinOp::Mul, std::move(i), std::move(size)));
    off->setType(idx);
    const Type *ptr = types.pointerTo(elem);
    ExprPtr at(new Binary(BinOp::Add, std::move(decayed), std::move(off)));
    at->setType(ptr);
    return at;
}

// `S a[4];` where S has constructors - the default constructor once per
// element, in a loop.
const Parser::Signature *
Parser::initializerListConstructor(const Type *cls, const Type **ilType) {
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t i = 0; i < set->size(); i++) {
        const Signature &c = functions_[(*set)[i]];
        // [dcl.init.list]/2: its first parameter `initializer_list<E>` or a reference to one, any other one
        // with a default - `map(initializer_list<value_type>, const Compare & = Compare())` (item 39).
        if (c.params.empty() || leastArguments(c) > 1) continue;
        const Type *p = c.params[0]->isReference() ? c.params[0]->referent()->unqualified()
                                                    : c.params[0]->unqualified();
        if (p->isStructOrUnion() && p->isSpecialization() &&
            p->templateName() == "initializer_list" &&
            (p->templateNamespace() == "std" ||
             p->templateNamespace() == "std::")) {
            if (ilType != nullptr) *ilType = c.params[0]->isReference() ? c.params[0]->referent() : c.params[0];
            return &c;
        }
    }
    return nullptr;
}

// [temp.deduct.call]/1: `seed_seq s{1, 2, 3}` - a constructor template on initializer_list<T> deduces T from the
// elements, which must agree, and is then the class's initializer-list constructor.
const Parser::Signature *Parser::listConstructorTemplate(const Type *cls, const Init &in, const Type **ilType,
                                                          std::size_t pos) {
    if (in.items.empty() || memberTemplates_.count(constructorKey(cls->tag())) == 0) return nullptr;
    const std::map<std::string, TemplateDecl>::const_iterator il = findTemplate("std::initializer_list");
    if (il == templates_.end() || !il->second.isClass) return nullptr;
    const Type *elem = nullptr;
    for (const Init &item : in.items) {
        if (item.isList || item.value == nullptr || item.value->type() == nullptr) return nullptr;
        const Type *t = item.value->type();
        t = decayedType(t->isReference() ? t->referent() : t)->unqualified();
        if (elem != nullptr && elem != t) return nullptr;
        elem = t;
    }
    instantiateConstructorTemplates(cls, std::vector<const Type *>(1, initializerListOf(elem, pos)), pos);
    return initializerListConstructor(cls, ilType);
}

// `std::string{"ab"}` is not initializer_list<char>'s: "ab" is no char, so the constructors take the
// elements as their arguments - string(const char *) - [over.match.list]/1 and [over.ics.list]/4.
bool Parser::initializerListFits(const Type *ilType, const Init &in) {
    const Type *il = ilType->unqualified();
    if (!il->isSpecialization() || il->templateArgs().empty() || il->templateArgs()[0].type == nullptr) return true;
    const Type *elem = il->templateArgs()[0].type;
    for (std::size_t i = 0; i < in.items.size(); i++) {
        if (in.items[i].isList) continue;
        const Expr *v = in.items[i].value.get();
        if (v == nullptr || v->type() == nullptr || rankArgument(*v, elem) == Rank::None) return false;
    }
    return true;
}

// **A temporary of class `cls` built where it belongs** (`*(T(&$tmp,...),
// &$tmp)` with `this` at `at`), the copy [class.copy]/31 lets cl and clang
// leave out; plain shapes only, a zeroed one keeps its copy. True if done.
bool Parser::constructInPlace(const Expr &value, const Type *cls, ExprPtr &at) {
    if (value.type() == nullptr || value.type()->unqualified() != cls) return false;
    const Unary *u = dynamic_cast<const Unary *>(&value);
    const Comma *c = u != nullptr && u->op() == '*' ? dynamic_cast<const Comma *>(&u->operand()) : nullptr;
    const Unary *addr = c != nullptr ? dynamic_cast<const Unary *>(&c->right()) : nullptr;
    const Var *v = addr != nullptr && addr->op() == '&' ? dynamic_cast<const Var *>(&addr->operand()) : nullptr;
    if (v == nullptr || !v->isLocal() || v->name() != "$tmp") return false;
    const Call *call = dynamic_cast<const Call *>(&c->left());
    if (call == nullptr)
        if (const Comma *guarded = dynamic_cast<const Comma *>(&c->left()))
            call = dynamic_cast<const Call *>(&guarded->left());
    if (call == nullptr || call->args().empty()) return false;
    const Unary *self = dynamic_cast<const Unary *>(call->args()[0].get());
    const Var *sv = self != nullptr && self->op() == '&' ? dynamic_cast<const Var *>(&self->operand()) : nullptr;
    if (sv == nullptr || sv->name() != "$tmp" || sv->offset() != v->offset()) return false;
    std::size_t pending = pendingTemps_.size();
    for (std::size_t k = pendingTemps_.size(); k-- > 0; )
        if (!pendingTemps_[k].newElement && pendingTemps_[k].slot == v->offset()) { pending = k; break; }
    if (pending == pendingTemps_.size() && destructorOf(cls) != nullptr) return false;
    if (pending < pendingTemps_.size())
        pendingTemps_.erase(pendingTemps_.begin() + static_cast<std::ptrdiff_t>(pending));
    const_cast<Call *>(call)->setArg(0, std::move(at));
    return true;
}

ExprPtr Parser::buildInitializerList(const Type *ilType, Init &in,
                                     std::size_t pos, std::vector<StmtPtr> &into,
                                     bool fullExpression) {
    const Type *elem = ilType->templateArgs().empty()
                         ? types_.get(Kind::Int)
                         : ilType->templateArgs()[0].type;
    const long long n = static_cast<long long>(in.items.size());
    const int k = ilTemps_++;

    const Type *arrType = types_.arrayOf(elem, n);
    const std::string bk = ".ilbacking" + std::to_string(k);
    // [dcl.init.list]/6: the array lives as long as the initializer_list - for one with static storage an
    // internal object of its own, named as the temporaries static references bind are.
    const bool staticArray = !staticIlFor_.empty();
    std::string bkSym;
    int bkOff = 0;
    if (staticArray) {
        if (elem->unqualified()->isStructOrUnion() && destructorOf(elem->unqualified()) != nullptr)
            src_.fail(pos, "this 'std::initializer_list' has static storage, and its array of '" +
                           elem->unqualified()->describe() + "' would have to be destroyed at exit - that is not "
                           "supported yet; give the list automatic storage");
        bkSym = target_.microsoftNames()
            ? "?$RT" + std::to_string(++staticTemps_) + "@" + staticIlFor_
            : staticIlFor_.compare(0, 2, "_Z") == 0 ? "_ZGR" + staticIlFor_.substr(2) + "_" + std::to_string(k)
                                                     : "_ZGR" + std::to_string(staticIlFor_.size()) + staticIlFor_ +
                                                       "_" + std::to_string(k);
        globalIndex_[bk] = globals_.size();
        globals_.push_back(GlobalSym{ bk, bkSym, arrType, false, true, true, false, 0 });
        current_->globals.push_back(Global{ bk, bkSym, arrType, std::vector<GlobalPiece>(), false, true, false });
    } else {
        bkOff = declare(bk, arrType, pos);
    }
    auto backing = [&]() {
        Var *v = staticArray ? Var::global(bk) : Var::local(bk, bkOff);
        if (staticArray) v->setSymbol(bkSym);
        ExprPtr arr(v);
        arr->setType(arrType);
        return arr;
    };

    // **A class element with an initializer_list constructor is built through
    // it, not aggregate-initialised.**
    const Type *plainElem = elem->unqualified();
    const Type *innerIl = nullptr;
    // Only where every element is itself braced: `vector<string> v = {"one", "two"}` copy-initialises each
    // string from its C string ([dcl.init.list]/5), as the branch after this one does.
    bool everyBraced = true;
    for (std::size_t i = 0; i < in.items.size(); i++) everyBraced = everyBraced && in.items[i].isList;
    const Signature *innerCtor = plainElem->isStructOrUnion() && everyBraced
        ? initializerListConstructor(plainElem, &innerIl) : nullptr;
    // [over.match.list]/1: an element whose items its initializer_list cannot hold is built by the class's other
    // constructors, as it is outside a list - `{"x", "y"}` for a class with (initializer_list<char>) and
    // (const char *, const char *) (1.80: the list constructor was called with it and refused its items).
    for (std::size_t i = 0; innerCtor != nullptr && i < in.items.size(); i++)
        if (!initializerListFits(innerIl, in.items[i])) innerCtor = nullptr;
    if (innerCtor != nullptr) {
        markUsed(innerCtor);
        const std::string sym = innerCtor->symbol;
        const Type *innerParam = innerCtor->params[0];
        const Type *elemPtr = types_.pointerTo(plainElem);
        for (std::size_t i = 0; i < in.items.size(); i++) {
            if (!in.items[i].isList)
                src_.fail(in.items[i].pos, "this element is built from a braced "
                          "list and this initialiser is not braced");
            ExprPtr list = buildInitializerList(innerIl, in.items[i], pos, into, fullExpression);
            ExprPtr arr = backing();
            ExprPtr idx(new Num(static_cast<long long>(i)));
            idx->setType(types_.get(Kind::Int));
            ExprPtr addr = arithmetic(BinOp::Add, decay(std::move(arr)),
                                      std::move(idx), pos);
            std::vector<ExprPtr> all;
            all.push_back(std::move(addr));
            all.push_back(std::move(list));
            std::vector<const Type *> ps{ elemPtr, innerParam };
            into.push_back(StmtPtr(new ExprStmt(
                completeCall(plainElem->tag(), sym, nullptr,
                             types_.get(Kind::Void), ps, false, pos,
                             std::move(all)))));
        }
    } else if (plainElem->isStructOrUnion() && hasConstructors(plainElem->tag())) {
        // [dcl.init.list]/5: each element is copy-initialised from its initialiser, by a constructor.
        const Type *elemPtr = types_.pointerTo(plainElem);
        const Signature *elemDtor = destructorOf(plainElem);
        // **Inside an expression the array lives to the end of the full expression**, as the
        // initializer_list it backs does ([dcl.init.list]/6, [class.temporary]/3): each element is a
        // temporary of it, destroyed last first, at its index in the array's own slot (R21).
        const bool elementsAreTemporaries = fullExpression && elemDtor != nullptr;
        auto built = [&](std::size_t i) {
            if (!elementsAreTemporaries) return;
            const int g = guardFlag();
            Temporary t{ bkOff, plainElem, g };
            t.newElement = true;
            t.elementInFrame = true;
            t.newIndex = static_cast<long long>(i);
            pendingTemps_.push_back(t);
            into.push_back(StmtPtr(new ExprStmt(setGuard(g, 1))));
        };
        for (std::size_t i = 0; i < in.items.size(); i++) {
            // A braced element is copy-list-initialised, [over.match.list]: built in its slot by the
            // constructor its items choose, never an explicit one - no temporary and no copy.
            // A list of lists: the inner list makes its own array, as this one has, and no constructor.
            if (in.items[i].isList && isInitializerListType(plainElem)) {
                ExprPtr inner = initializerListExpr(plainElem, in.items[i], in.items[i].pos);
                ExprPtr arr = backing();
                ExprPtr idx(new Num(static_cast<long long>(i)));
                idx->setType(types_.get(Kind::Int));
                ExprPtr slot(new Unary('*', arithmetic(BinOp::Add, decay(std::move(arr)), std::move(idx), pos)));
                slot->setType(plainElem);
                ExprPtr store(new Assign(std::move(slot), std::move(inner)));
                store->setType(plainElem);
                into.push_back(StmtPtr(new ExprStmt(std::move(store))));
                continue;
            }
            // A list of lists: the inner list makes its own array, as this one has, and no constructor.
            if (in.items[i].isList && isInitializerListType(plainElem)) {
                ExprPtr inner = initializerListExpr(plainElem, in.items[i], in.items[i].pos);
                ExprPtr arr = backing();
                ExprPtr idx(new Num(static_cast<long long>(i)));
                idx->setType(types_.get(Kind::Int));
                ExprPtr slot(new Unary('*', arithmetic(BinOp::Add, decay(std::move(arr)), std::move(idx), pos)));
                slot->setType(plainElem);
                ExprPtr store(new Assign(std::move(slot), std::move(inner)));
                store->setType(plainElem);
                into.push_back(StmtPtr(new ExprStmt(std::move(store))));
                continue;
            }
            if (in.items[i].isList) {
                checkNotAbstract(plainElem, in.items[i].pos, "an element of this initializer_list would make");
                // one of several elements, and this one its class's initializer_list holds: that constructor's
                const Type *itemIl = nullptr;
                const Signature *itemCtor = in.items[i].items.empty() ? nullptr
                                                                      : initializerListConstructor(plainElem, &itemIl);
                if (itemCtor != nullptr && initializerListFits(itemIl, in.items[i])) {
                    markUsed(itemCtor);
                    const std::string sym = itemCtor->symbol;
                    const Type *itemParam = itemCtor->params[0];
                    ExprPtr list = buildInitializerList(itemIl, in.items[i], pos, into, fullExpression);
                    ExprPtr arr = backing();
                    ExprPtr idx(new Num(static_cast<long long>(i)));
                    idx->setType(types_.get(Kind::Int));
                    std::vector<ExprPtr> all;
                    all.push_back(arithmetic(BinOp::Add, decay(std::move(arr)), std::move(idx), pos));
                    all.push_back(std::move(list));
                    std::vector<const Type *> ps{ elemPtr, itemParam };
                    into.push_back(StmtPtr(new ExprStmt(
                        completeCall(plainElem->tag(), sym, nullptr, types_.get(Kind::Void), ps, false, pos,
                                     std::move(all)))));
                    built(i);
                    continue;
                }
                std::vector<ExprPtr> args;
                Signature ctor = listConstructor(plainElem, in.items[i], args, true, in.items[i].pos);
                applyDefaults(ctor, args, in.items[i].pos);
                if (ctor.access != Access::Public && !insideAccessOf(plainElem, ctor.access) &&
                    !isFriendOf(plainElem))
                    src_.fail(in.items[i].pos, "'" + plainElem->describe() + "' has no public constructor "
                              "taking these arguments - the one that matches is " +
                              (ctor.access == Access::Private ? "private" : "protected"));
                ExprPtr arr = backing();
                ExprPtr idx(new Num(static_cast<long long>(i)));
                idx->setType(types_.get(Kind::Int));
                std::vector<ExprPtr> all;
                all.push_back(arithmetic(BinOp::Add, decay(std::move(arr)), std::move(idx), pos));
                for (ExprPtr &a : args) all.push_back(std::move(a));
                std::vector<const Type *> ps{ elemPtr };
                ps.insert(ps.end(), ctor.params.begin(), ctor.params.end());
                into.push_back(StmtPtr(new ExprStmt(
                    completeCall(plainElem->tag(), ctor.symbol, nullptr, types_.get(Kind::Void), ps,
                                 ctor.variadic, pos, std::move(all)))));
                built(i);
                continue;
            }
            ExprPtr value = std::move(in.items[i].value);
            if (ExprPtr made = userConversion(plainElem, value, pos)) value = std::move(made);
            // [dcl.init.list]/5: copy-initialised, so no explicit constructor makes the element from another type.
            if (value->type() == nullptr ||
                (value->type()->unqualified() != plainElem && (!value->type()->unqualified()->isStructOrUnion() ||
                                                               accessibleBaseOffset(value->type(), plainElem) < 0)))
                src_.fail(in.items[i].pos, "an element of this initializer_list is a '" + plainElem->describe() +
                                           "' copy-initialised from '" + (value->type() != nullptr ? value->type()->describe()
                                                                                  : std::string("nothing")) +
                                           "', and no constructor that is not explicit converts it - [dcl.init.list]/5");
            ExprPtr arr = backing();
            ExprPtr idx(new Num(static_cast<long long>(i)));
            idx->setType(types_.get(Kind::Int));
            ExprPtr addr = arithmetic(BinOp::Add, decay(std::move(arr)), std::move(idx), pos);
            // **A prvalue of the element's own class is the element**, built in its slot rather than
            // copied from a temporary - `{T(1), T(2)}` and `{4, 5}` through a converting constructor alike.
            if (constructInPlace(*value, plainElem, addr)) {
                into.push_back(StmtPtr(new ExprStmt(std::move(value))));
                built(i);
                continue;
            }
            const Signature *cc = copyConstructorOf(plainElem);
            const Signature *mv = moveConstructorOf(plainElem);
            const Signature *use = (!isLvalue(*value) && mv != nullptr) ? mv : cc;
            if (use == nullptr) {
                ExprPtr slot(new Unary('*', std::move(addr))); slot->setType(plainElem);
                ExprPtr store(new Assign(std::move(slot), std::move(value))); store->setType(plainElem);
                into.push_back(StmtPtr(new ExprStmt(std::move(store))));
                built(i);
                continue;
            }
            markUsed(use);
            std::vector<ExprPtr> all;
            all.push_back(std::move(addr));
            all.push_back(std::move(value));
            std::vector<const Type *> ps{ elemPtr, use->params[0] };
            into.push_back(StmtPtr(new ExprStmt(
                completeCall(plainElem->tag(), use->symbol, nullptr,
                             types_.get(Kind::Void), ps, false, pos, std::move(all)))));
            built(i);
        }
        if (!fullExpression) {
            flushTemporaries(into);
            if (elemDtor != nullptr) {
                Alive whole{ bk, bkOff, plainElem };
                whole.count = n;
                alive_.push_back(whole);
            }
        }
    } else {
        std::vector<InitStep> path;
        emitInit(bk, path, arrType, in, into);
    }

    const std::string ilName = ".ilobject" + std::to_string(k);
    const int ilOff = declare(ilName, ilType, pos);

    const Member *fm = ilType->findMember("first_");
    const Member *cm = ilType->findMember("count_");
    if (fm == nullptr || cm == nullptr)
        src_.fail(pos, "the std::initializer_list here has no first_ and count_ "
                       "members - the <initializer_list> the compiler builds is "
                       "not the one this program included");

    // first_ = &array[0], the array decayed to a pointer to its first element.
    ExprPtr arr = backing();
    ExprPtr first = decay(std::move(arr));
    ExprPtr ilA(Var::local(ilName, ilOff)); ilA->setType(ilType);
    ExprPtr fdst(new MemberAccess(std::move(ilA), "first_", fm->offset));
    fdst->setType(fm->type);
    ExprPtr fst(new Assign(std::move(fdst), convert(std::move(first), fm->type)));
    fst->setType(fm->type);
    into.push_back(StmtPtr(new ExprStmt(std::move(fst))));

    // count_ = n
    ExprPtr ilB(Var::local(ilName, ilOff)); ilB->setType(ilType);
    ExprPtr cdst(new MemberAccess(std::move(ilB), "count_", cm->offset));
    cdst->setType(cm->type);
    ExprPtr cnt(new Num(n)); cnt->setType(cm->type);
    ExprPtr cst(new Assign(std::move(cdst), std::move(cnt)));
    cst->setType(cm->type);
    into.push_back(StmtPtr(new ExprStmt(std::move(cst))));

    ExprPtr result(Var::local(ilName, ilOff));
    result->setType(ilType);
    return result;
}

StmtPtr Parser::constructLocalArray(const Declared &d, int offset,
                                    int indexSlot) {
    const Type *elem = d.type;
    long long count = 1;
    while (elem->isArray()) { count *= elem->length(); elem = elem->pointee(); }
    const Type *plain = elem->unqualified();

    const Signature *ctor = defaultConstructorOf(plain);
    if (ctor == nullptr)
        src_.fail(d.pos, "'" + plain->describe() + "' has constructors but none "
                         "that takes nothing, and an array of it has no way to "
                         "say what to pass");
    if (ctor->access != Access::Public &&
        !insideAccessOf(plain, ctor->access) && !isFriendOf(plain))
        src_.fail(d.pos, "'" + plain->describe() + "' has no public default "
                         "constructor, and an array of it needs one");
    // **Marked used, or an implicit one is declared and never emitted.**
    markUsed(ctor);
    // **Copied before the defaults are read**: reading one can parse an expression
    // that grows `functions_` under the pointer just taken into it. The defaults sit
    // inside the statement the loop repeats, so they are evaluated once per element.
    const Signature chosen = *ctor;
    std::vector<ExprPtr> defaults;
    applyDefaults(chosen, defaults, d.pos);

    const Type *ptr = types_.pointerTo(plain);
    ExprPtr base(Var::local(d.name, offset));
    base->setType(d.type);
    ExprPtr at = indexBytes(types_, decay(std::move(base)), plain, indexSlot,
                            target_);

    std::vector<ExprPtr> args;
    args.push_back(std::move(at));
    std::vector<const Type *> ps;
    ps.push_back(ptr);
    for (std::size_t i = 0; i < defaults.size(); i++) {
        args.push_back(std::move(defaults[i]));
        ps.push_back(chosen.params[i]);
    }

    StmtPtr one(new ExprStmt(completeCall(plain->tag(), chosen.symbol, nullptr,
                                          types_.get(Kind::Void), ps, false,
                                          d.pos, std::move(args))));
    return guardElements(eachElement(indexSlot, count, std::move(one)),
                         Alive{ d.name, offset, plain }, indexSlot, d.pos);
}

// `S a[3] = { S(1), {2, 3}, x };` - [dcl.init.aggr]/2: each element copy-initialised from its
// initialiser, in order, nested braces for each dimension or elided; the rest value-initialised,
// as from `{}`. What each one becomes is what `S s = ...;` would make of it.
StmtPtr Parser::constructLocalArrayFromList(Declared &d, int offset,
                                            const std::function<int()> &declareLate,
                                            const std::string &symbol) {
    // `S a[] = {...}` takes its length from the list: read first, declared after.
    const bool unbounded = d.type->length() < 0;
    long long total = 1;
    const Type *elem = d.type;
    while (elem->isArray()) { total *= elem->length() < 0 ? 0 : elem->length(); elem = elem->pointee(); }
    const Type *plain = elem->unqualified();
    const int size = plain->size(target_);

    // One initialiser per element, read from the list: `given` says whether it had one.
    struct Init { std::vector<ExprPtr> args; std::vector<Temporary> temps; bool braced = false; bool direct = false; bool given = false; };
    std::vector<Init> inits(static_cast<std::size_t>(total));
    // `S(args)` is the whole initialiser when a ',' or '}' follows its closing parenthesis.
    auto closesElement = [&]() {
        std::size_t k = 1;
        int depth = 0;
        for (;; k++) {
            const Token &t = peekAt(k);
            if (t.kind == TokenKind::End) return false;
            if (t.is("(")) depth++;
            if (t.is(")") && --depth == 0) break;
        }
        return peekAt(k + 1).is(",") || peekAt(k + 1).is("}");
    };
    std::function<void(const Type *, long long)> readList = [&](const Type *t, long long base) {
        long long flat = 1;
        const Type *e = t;
        while (e->isArray()) { flat *= e->length(); e = e->pointee(); }
        if (t->length() < 0) flat = LLONG_MAX;
        const Type *sub = t->pointee();
        long long subFlat = 1;
        for (const Type *u = sub; u->isArray(); u = u->pointee()) subFlat *= u->length();
        const std::size_t open = peek().pos;
        expect("{");
        long long at = 0;
        while (!peek().is("}")) {
            if (at >= flat)
                src_.fail(peek().pos, "too many initialisers for '" + t->describe() +
                                      "' - it holds " + std::to_string(flat));
            if (sub->isArray() && peek().is("{")) {
                // a nested list starts the next row; one already begun is left to its defaults
                at = (at + subFlat - 1) / subFlat * subFlat;
                if (at >= flat)
                    src_.fail(peek().pos, "too many initialisers for '" + t->describe() + "'");
                readList(sub, base + at);
                at += subFlat;
            } else {
                if (static_cast<std::size_t>(base + at) >= inits.size())
                    inits.resize(static_cast<std::size_t>(base + at + 1));
                Init &one = inits[static_cast<std::size_t>(base + at)];
                one.given = true;
                std::vector<Temporary> before;
                before.swap(pendingTemps_);
                if (consume("{")) {
                    one.braced = true;
                    while (!peek().is("}")) {
                        one.args.push_back(assign());
                        if (!consume(",")) break;
                    }
                    expect("}");
                } else if (peek().kind == TokenKind::Ident && peekAt(1).is("(") &&
                           (peek().text == plain->localName() || peek().text == plain->tag()) &&
                           closesElement()) {
                    // `S(args)` builds the element itself, as clang does: direct-init, no copy.
                    one.direct = true;
                    at_ += 2;
                    while (!peek().is(")")) {
                        one.args.push_back(assign());
                        if (!consume(",")) break;
                    }
                    expect(")");
                } else {
                    one.args.push_back(assign());
                }
                // **Each element's initialiser is a full-expression of its own**: its
                // temporaries go with it and die before the next element is built.
                one.temps.swap(pendingTemps_);
                pendingTemps_.swap(before);
                at++;
            }
            if (!consume(",")) break;
        }
        (void)open;
        expect("}");
    };
    readList(d.type, 0);
    if (unbounded) {
        long long rowFlat = 1;
        for (const Type *u = d.type->pointee(); u->isArray(); u = u->pointee()) rowFlat *= u->length();
        const long long rows = (static_cast<long long>(inits.size()) + rowFlat - 1) / rowFlat;
        if (rows == 0)
            src_.fail(d.pos, "'" + d.name + "' takes its length from an empty list, and an "
                             "array of no elements is not C++");
        d.type = types_.arrayOf(d.type->pointee(), rows);
        total = rows * rowFlat;
        inits.resize(static_cast<std::size_t>(total));
        offset = declareLate();
    }

    // Each element in turn, as the object `S e = init;` - its temporaries gone before the next.
    Declared one = d;
    one.type = plain;
    std::vector<StmtPtr> all;
    long long firstDefault = total;
    for (long long i = total; i > 0 && !inits[static_cast<std::size_t>(i - 1)].given; i--)
        firstDefault = i - 1;
    // [except.ctor]/3: a local array counts the elements it has built, which an exception destroys.
    const bool counted = symbol.empty() && destructorOf(plain) != nullptr;
    const int builtSlot = counted ? allocateFrameSlot(types_.intType()) : 0;
    auto builtNow = [&](ExprPtr value) {
        ExprPtr slot(Var::local("$i", builtSlot));
        slot->setType(types_.intType());
        value->setType(types_.intType());
        ExprPtr set(new Assign(std::move(slot), std::move(value)));
        set->setType(types_.intType());
        return StmtPtr(new ExprStmt(std::move(set)));
    };
    if (counted) all.push_back(builtNow(ExprPtr(new Num(0LL))));
    for (long long i = 0; i < firstDefault; i++) {
        Init &e = inits[static_cast<std::size_t>(i)];
        const int at = symbol.empty() ? offset - static_cast<int>(i * size)
                                      : static_cast<int>(i * size);
        const bool empty = e.args.empty();
        pendingTemps_.insert(pendingTemps_.end(), e.temps.begin(), e.temps.end());
        // **A trivially copyable class is copied, not constructed** - there is no copy
        // constructor to resolve to, as `S s = t;` finds for one object.
        if (e.args.size() == 1 && !e.braced && !e.direct && copyConstructorOf(plain) == nullptr &&
            moveConstructorOf(plain) == nullptr && e.args[0]->type() != nullptr &&
            e.args[0]->type()->unqualified() == plain) {
            ExprPtr dst = objectAt(one, symbol, at);
            ExprPtr store(new Assign(std::move(dst), std::move(e.args[0])));
            store->setType(plain);
            all.push_back(StmtPtr(new ExprStmt(std::move(store))));
        } else {
            const bool copyInit = !e.direct && (!e.braced || !empty);
            if (copyInit && !e.braced) copyFromOtherClass(plain, e.args, d.pos);
            if (copyInit) {
                const Signature chosen = resolveOverload(constructorKey(plain->tag()), e.args, d.pos);
                if (chosen.isExplicit)
                    src_.fail(d.pos, "element " + std::to_string(i) + " of '" + d.name + "' is "
                                     "copy-initialised from its initialiser, and the constructor of '" +
                                     plain->describe() + "' taking it is 'explicit' - write '" +
                                     plain->localName() + "(...)' in the list, which asks for it by name");
            }
            all.push_back(constructObject(one, symbol, at, std::move(e.args), copyInit, empty));
        }
        flushTemporaries(all);
        if (counted) all.push_back(builtNow(ExprPtr(new Num(i + 1))));
    }
    if (firstDefault < total) {
        const Signature *ctor = defaultConstructorOf(plain);
        if (ctor == nullptr)
            src_.fail(d.pos, "'" + d.name + "' has " + std::to_string(firstDefault) +
                             " initialisers for " + std::to_string(total) + " elements, and '" +
                             plain->describe() + "' has no constructor taking nothing to build "
                             "the rest");
        if (ctor->implicit) {
            // value-initialisation zeroes first where nobody wrote the constructor: one by one
            for (long long i = firstDefault; i < total; i++) {
                all.push_back(constructObject(one, symbol, symbol.empty()
                                                  ? offset - static_cast<int>(i * size)
                                                  : static_cast<int>(i * size),
                                              std::vector<ExprPtr>(), true, true));
                if (counted) all.push_back(builtNow(ExprPtr(new Num(i + 1))));
            }
        } else {
            markUsed(ctor);
            const Signature chosen = *ctor;
            std::vector<ExprPtr> defaults;
            applyDefaults(chosen, defaults, d.pos);
            const int indexSlot = allocateFrameSlot(types_.intType());
            ExprPtr first = objectAt(one, symbol, symbol.empty()
                ? offset - static_cast<int>(firstDefault * size)
                : static_cast<int>(firstDefault * size));
            ExprPtr start(new Unary('&', std::move(first)));
            start->setType(types_.pointerTo(plain));
            std::vector<ExprPtr> args;
            args.push_back(indexBytes(types_, std::move(start), plain, indexSlot, target_));
            std::vector<const Type *> ps;
            ps.push_back(types_.pointerTo(plain));
            for (std::size_t k = 0; k < defaults.size(); k++) {
                args.push_back(std::move(defaults[k]));
                ps.push_back(chosen.params[k]);
            }
            StmtPtr call(new ExprStmt(completeCall(plain->tag(), chosen.symbol, nullptr,
                                                   types_.get(Kind::Void), ps, false, d.pos,
                                                   std::move(args))));
            if (counted) {
                ExprPtr was(Var::local("$i", builtSlot));
                was->setType(types_.intType());
                ExprPtr step(new Num(1LL));
                step->setType(types_.intType());
                std::vector<StmtPtr> both;
                both.push_back(std::move(call));
                both.push_back(builtNow(ExprPtr(new Binary(BinOp::Add, std::move(was), std::move(step)))));
                call = StmtPtr(new Block(std::move(both)));
            }
            all.push_back(eachElement(indexSlot, total - firstDefault, std::move(call)));
        }
    }
    if (!counted) return StmtPtr(new Block(std::move(all)));
    Alive built{ d.name, offset, plain };
    built.builtSlot = builtSlot;
    return guardSubobject(StmtPtr(new Block(std::move(all))), built, d.pos);
}

// The loop functions: the first element's address and a count, a frame of
// their own, once per class and unit, file-local - `__cxx1_vec_new_<class>`
// and `_del_`, the class spelled as the Itanium mangler spells a nested one.
std::string Parser::vectorLoopName(const char *which, const Type *cls,
                                   std::size_t pos) {
    std::string spelt, why;
    if (!itaniumTypeSpelling(cls, &spelt, &why))
        src_.fail(pos, "an array of '" + cls->describe() + "' cannot be named: " + why);
    return std::string("__cxx1_vec_") + which + "_" + spelt;
}

ExprPtr Parser::callVectorLoop(const std::string &fn, const Type *cls,
                               ExprPtr base, ExprPtr count, std::size_t pos) {
    std::vector<const Type *> ps;
    ps.push_back(types_.pointerTo(cls));
    ps.push_back(types_.get(target_.sizeType()));
    std::vector<ExprPtr> args;
    args.push_back(std::move(base));
    args.push_back(convert(std::move(count), ps[1]));
    return completeCall(fn, fn, nullptr, types_.get(Kind::Void), ps, false,
                        pos, std::move(args));
}

std::string Parser::vectorConstructor(const Type *cls, std::size_t pos) {
    const std::string name = vectorLoopName("new", cls, pos);
    for (std::size_t i = 0; i < current_->functions.size(); i++)
        if (current_->functions[i].symbol() == name) return name;

    const Signature *ctor = defaultConstructorOf(cls);
    if (ctor == nullptr)
        src_.fail(pos, "'" + cls->describe() + "' has constructors but none "
                       "that takes nothing, and an array of it has no way to "
                       "say what to pass");
    if (ctor->access != Access::Public && !insideAccessOf(cls, ctor->access) &&
        !isFriendOf(cls))
        src_.fail(pos, "'" + cls->describe() + "' has no public default "
                       "constructor, and an array of it needs one");
    markUsed(ctor);
    const Signature chosen = *ctor;

    const Type *ptr = types_.pointerTo(cls);
    const Type *sizeT = types_.get(target_.sizeType());
    const FrameSave savedFrame = openFrame();
    OwnPads own(*this);
    const int baseSlot = allocateFrameSlot(ptr);
    const int countSlot = allocateFrameSlot(sizeT);
    const int indexSlot = allocateFrameSlot(types_.intType());
    std::vector<Param> params;
    params.push_back(Param{ ptr, baseSlot });
    params.push_back(Param{ sizeT, countSlot });

    std::vector<ExprPtr> defaults;
    applyDefaults(chosen, defaults, pos);
    ExprPtr base(Var::local("base", baseSlot));
    base->setType(ptr);
    ExprPtr at = indexBytes(types_, std::move(base), cls, indexSlot, target_);
    std::vector<ExprPtr> args;
    args.push_back(std::move(at));
    std::vector<const Type *> ps;
    ps.push_back(ptr);
    for (std::size_t i = 0; i < defaults.size(); i++) {
        args.push_back(std::move(defaults[i]));
        ps.push_back(chosen.params[i]);
    }
    StmtPtr one(new ExprStmt(completeCall(cls->tag(), chosen.symbol, nullptr,
                                          types_.get(Kind::Void), ps, false,
                                          pos, std::move(args))));
    ExprPtr n(Var::local("n", countSlot));
    n->setType(sizeT);
    std::vector<StmtPtr> body;
    Alive built{ "base", baseSlot, cls };
    built.byAddress = true;
    body.push_back(guardElements(eachElement(indexSlot, convert(std::move(n), types_.intType()),
                                             std::move(one)),
                                 built, indexSlot, pos));
    body.push_back(StmtPtr(new Return(nullptr)));
    current_->functions.push_back(Function(name, types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), true, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(name);
    current_->functions.back().setHasLandingPads(own.made());
    closeFrame(savedFrame);
    return name;
}

// `i = n; while (i > 0) { i = i - 1; ~T(base + i); }` - last first, [class.dtor].
std::string Parser::vectorDestructor(const Type *cls, std::size_t pos) {
    const std::string name = vectorLoopName("del", cls, pos);
    for (std::size_t i = 0; i < current_->functions.size(); i++)
        if (current_->functions[i].symbol() == name) return name;
    const Signature *dtor = destructorOf(cls);
    if (dtor == nullptr) return name;
    markUsed(dtor);

    const Type *ptr = types_.pointerTo(cls);
    const Type *sizeT = types_.get(target_.sizeType());
    const Type *idx = types_.intType();
    const FrameSave savedFrame = openFrame();
    OwnPads own(*this);
    const int baseSlot = allocateFrameSlot(ptr);
    const int countSlot = allocateFrameSlot(sizeT);
    const int indexSlot = allocateFrameSlot(idx);
    std::vector<Param> params;
    params.push_back(Param{ ptr, baseSlot });
    params.push_back(Param{ sizeT, countSlot });

    auto index = [&]() { ExprPtr e(Var::local("$i", indexSlot)); e->setType(idx); return e; };
    ExprPtr n(Var::local("n", countSlot));
    n->setType(sizeT);
    ExprPtr init(new Assign(index(), convert(std::move(n), idx)));
    init->setType(idx);
    ExprPtr zero(new Num(0LL));
    zero->setType(idx);
    ExprPtr cond(new Binary(BinOp::Gt, index(), std::move(zero)));
    cond->setType(idx);
    ExprPtr one(new Num(1LL));
    one->setType(idx);
    ExprPtr less(new Binary(BinOp::Sub, index(), std::move(one)));
    less->setType(idx);
    ExprPtr step(new Assign(index(), std::move(less)));
    step->setType(idx);
    ExprPtr base(Var::local("base", baseSlot));
    base->setType(ptr);
    ExprPtr at = indexBytes(types_, std::move(base), cls, indexSlot, target_);
    std::vector<StmtPtr> inner;
    inner.push_back(StmtPtr(new ExprStmt(std::move(step))));
    inner.push_back(StmtPtr(new ExprStmt(destructorCall(std::move(at), *dtor, pos))));
    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new ExprStmt(std::move(init))));
    StmtPtr loop(new While(std::move(cond), StmtPtr(new Block(std::move(inner)))));
    // [except.ctor]/2: an element whose destructor throws leaves those before it, the first `$i`.
    if (destructorMayThrow(cls)) {
        ExprPtr first(Var::local("base", baseSlot));
        first->setType(ptr);
        std::vector<StmtPtr> rest;
        rest.push_back(StmtPtr(new ExprStmt(callVectorLoop(name, cls, std::move(first), index(), pos))));
        Block *b = new Block(std::move(rest));
        b->setScope(-1);
        std::vector<StmtPtr> guarded;
        guarded.push_back(std::move(loop));
        loop = cleanupRegion(std::move(guarded), StmtPtr(b));
    }
    body.push_back(std::move(loop));
    body.push_back(StmtPtr(new Return(nullptr)));
    current_->functions.push_back(Function(name, types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), true, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(name);
    current_->functions.back().setHasLandingPads(own.made());
    closeFrame(savedFrame);
    return name;
}

// [expr.new]/12 as the Itanium ABI fixes it: a class delete[] must destroy
// keeps its count in the last size_t of a cookie in front of the array, as
// wide as the larger of size_t and the element's alignment; else none.
int Parser::arrayCookie(const Type *elem) const {
    if (destructorOf(elem->unqualified()) == nullptr) return 0;
    const int sizeT = types_.get(target_.sizeType())->size(target_);
    const int words = target_.armArrayCookie() ? 2 * sizeT : sizeT;
    const int align = elem->align(target_);
    return align > words ? align : words;
}

// **A default constructor is one that can be called with no arguments, not one whose
// parameter list is empty** - [class.ctor]/5, so `S(int a = 1)` is one. Whoever calls
// this still supplies the defaults; two that both take nothing answer nullptr.
const Parser::Signature *Parser::defaultConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    const Signature *found = nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (leastArguments(f) != 0) continue;
        if (found != nullptr) return nullptr;
        found = &f;
    }
    return found;
}

const Parser::Signature *Parser::copyConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.params.size() != 1 || !f.params[0]->isReference()) continue;
        // **`S(S &&)` is not a copy constructor**, and until rung 7 there was no way
        // to write one, so isReference() alone was enough. Answering one here would
        // hand an lvalue to a constructor whose whole contract is that it gets none.
        if (f.params[0]->isRValueReference()) continue;
        if (f.params[0]->referent()->unqualified() != cls->unqualified()) continue;
        return &f;
    }
    return nullptr;
}

// `S(S &&)`, written by hand - the compiler does not yet write one. The
// mirror of copyConstructorOf and told apart from it by exactly one thing,
// which is the kind of reference the parameter is.
const Parser::Signature *Parser::moveConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.params.size() != 1 || !f.params[0]->isRValueReference()) continue;
        if (f.params[0]->referent()->unqualified() != cls->unqualified()) continue;
        return &f;
    }
    return nullptr;
}

// **Only `X`, `X &`, `const X &` - or `X &&`, the move - make an assignment a copy**,
// [class.copy]/17: `operator=(const double *)` is an operator of the class and not its
// copy, which the class still has. The copy is answered before the move.
const Parser::Signature *Parser::copyAssignOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(assignmentKey(cls->tag()));
    if (set == nullptr) return nullptr;
    const Signature *move = nullptr;
    for (std::size_t i = 0; i < set->size(); i++) {
        const Signature &f = functions_[(*set)[i]];
        if (f.params.size() != 1) continue;
        const Type *p = f.params[0];
        const bool ref = p->isReference();
        if ((ref ? p->pointee() : p)->unqualified() != cls->unqualified()) continue;
        if (ref && p->isRValueReference()) { if (move == nullptr) move = &f; continue; }
        return &f;
    }
    return move;
}

// The move assignment of a class, `X &operator=(X &&)`, written or defaulted; copyAssignOf answers the copy first.
const Parser::Signature *Parser::moveAssignOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty()) return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(assignmentKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t i = 0; i < set->size(); i++) {
        const Signature &f = functions_[(*set)[i]];
        if (f.params.size() == 1 && f.params[0]->isRValueReference() &&
            f.params[0]->referent()->unqualified() == cls->unqualified()) return &f;
    }
    return nullptr;
}

std::string Parser::baseConstructorSymbol(const Signature &ctor, const Type *base) {
    if (target_.microsoftNames()) return ctor.symbol;
    // A constructor template's C2 was spelled with its C1, from the pattern.
    const std::map<std::string, std::string>::const_iterator tc2 = constructorC2_.find(ctor.symbol);
    if (tc2 != constructorC2_.end()) return tc2->second;
    const Type *fnType = types_.functionType(types_.get(Kind::Void), ctor.params,
                                             false);
    std::string sub, why;
    // An inherited constructor's base-subobject form is its CI2.
    const std::map<std::string, const Type *>::const_iterator inh = inheritedFrom_.find(ctor.symbol);
    if (inh != inheritedFrom_.end())
        return itaniumInheritingConstructorName(base->tag(), base, inh->second, fnType, false, &sub, &why)
               ? sub : ctor.symbol;
    if (itaniumConstructorName(base->tag(), base, fnType, false, &sub, &why))
        return sub;
    return ctor.symbol;
}

// **A trivial special member is not a function**, and that is measured rather than
// reasoned: cl and clang emit no symbol for one with no work to do. So an implicit
// member is declared only where it has some - a vptr, or a base or member to build.
void Parser::declareImplicitSpecials(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (tag.empty()) return;
    // Asked before the copy constructor is declared, because declaring one would answer it yes.
    const bool wroteConstructor = hasConstructors(tag);
    // [class.union]/4: a union's one member initialiser is applied by its
    // implicit default constructor; /2: a member with a non-trivial special
    // member deletes the union's one of that kind (declared only with work).
    if (type->kind() == Kind::Union) {
        bool ctor = false, copy = false, assign = false, dtor = false;
        for (const Member &m : type->members()) {
            const Type *mc = memberClass(m.type);
            if (mc == nullptr || mc->tag().empty()) continue;
            if (hasConstructors(mc->tag())) ctor = true;
            if (mc->nonTrivialCopy()) copy = true;
            if (copyAssignOf(mc) != nullptr) assign = true;
            if (destructorOf(mc) != nullptr) dtor = true;
        }
        if (!wroteConstructor && hasMemberInitialiser(tag)) {
            declareImplicitDefaultCtor(tag, type, pos, Access::Public);
        } else if (!wroteConstructor && ctor) {
            declareImplicitDefaultCtor(tag, type, pos, Access::Public);
            functions_.back().deleted = true;
        }
        if (dtor && overloadsOf(destructorKey(tag)) == nullptr) {
            registerDestructor(tag, pos, Access::Public, false, true, VirtSpecifiers(), type);
            functions_[(*overloadsOf(destructorKey(tag)))[0]].deleted = true;
        }
        if (copy && copyConstructorOf(type) == nullptr) {
            declareImplicitCopyCtor(tag, type, pos);
            if (const Signature *c = copyConstructorOf(type))
                if (c->implicit) functions_[static_cast<std::size_t>(c - &functions_[0])].deleted = true;
        }
        if (assign && copyAssignOf(type) == nullptr) {
            declareImplicitCopyAssign(tag, type, pos);
            if (const Signature *a = copyAssignOf(type))
                if (a->implicit) functions_[static_cast<std::size_t>(a - &functions_[0])].deleted = true;
        }
        return;
    }
    // A member written `= default` is user-declared for [class.copy]/7 and /9 and implicit for everything else.
    const Defaulted df = defaulted_.count(tag) != 0 ? defaulted_[tag] : Defaulted();
    // **Read now, before the calls below declare all five**: [class.copy]/9
    // asks what the user declared, and a written copy or move assignment is
    // one (copyAssignOf answers the move too where only it is written).
    const bool wroteCopyOrDtor = copyConstructorOf(type) != nullptr ||
                                 moveConstructorOf(type) != nullptr ||
                                 copyAssignOf(type) != nullptr ||
                                 overloadsOf(destructorKey(tag)) != nullptr ||
                                 df.copyCtor.is || df.copyAssign.is || df.moveAssign.is || df.dtor.is;
    // [class.copy]/7 and /18: a defaulted move constructor or move assignment deletes the implicit copies.
    declareImplicitDestructor(tag, type, pos);
    declareImplicitCopyCtor(tag, type, pos);
    declareImplicitCopyAssign(tag, type, pos);
    // [class.copy]/20: the implicit move assignment, where none of the five was written - a
    // defaulted move constructor counts, declared below as it is (2026-10-21, fix 4: there
    // was none, and `b = std::move(a)` ran the copy assignment).
    // [dcl.fct.def.default]/2 with [except.spec]/14: one written `= default` has the implicit one's
    // exception-specification (1.77: it had none, and is_nothrow_move_assignable said false of it).
    if (df.moveAssign.is) {
        const std::size_t had = functions_.size();
        declareDefaultedMoveAssign(tag, type, pos);
        if (functions_.size() > had)
            functions_.back().isNoexcept = df.moveAssign.quiet || implicitNoexcept(type, kImplicitMoveAssign);
    }
    else declareImplicitMoveAssign(tag, type, pos, wroteCopyOrDtor || df.moveCtor.is);
    // Before the `wroteConstructor` return below: writing a constructor of
    // your own costs you the implicit *default* one and nothing else.
    declareImplicitMoveCtor(tag, type, pos, wroteCopyOrDtor && !df.moveCtor.is);
    applyDefaultedAccess(tag, type);
    if (wroteConstructor && !df.defaultCtor.is) return;
    // [class.ctor]/5: only a user-declared constructor suppresses this one - a deleted implicit copy does not.
    const bool needDefault = df.defaultCtor.is || (!df.copyCtor.is && !df.moveCtor.is);
    if (!needDefault) return;

    // **An initialiser on a member is work**: without a constructor there is no function to put the
    // store in, and `S s;` would leave x holding the stack. So is a defaulted default constructor
    // beside a written one, since `S s;` then has to find a constructor to call.
    bool work = type->hasVptr() || hasConstructors(tag) || nonPublicDefault(df.defaultCtor);
    for (std::size_t i = 0; i < type->members().size() && !work; i++)
        if (memberInit_.find(tag + "::" + type->members()[i].name) !=
            memberInit_.end())
            work = true;
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (!bs[i].type->tag().empty() &&
            hasConstructors(bs[i].type->tag()))
            work = true;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++) {
        const Type *mc = memberClass(ms[i].type);
        if (mc != nullptr && !mc->tag().empty() &&
            hasConstructors(mc->tag()))
            work = true;
    }
    if (!work) return;
    declareImplicitDefaultCtor(tag, type, pos,
                               df.defaultCtor.is ? df.defaultCtor.access : Access::Public);
    if (df.defaultCtor.is && df.defaultCtor.quiet) functions_.back().isNoexcept = true;
}

// The implicit default constructor's signature, named for the linker; its body is written on use.
void Parser::declareImplicitDefaultCtor(const std::string &tag, const Type *type,
                                        std::size_t pos, Access access) {
    const std::vector<const Type *> params;
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a default constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    functionIndex_[constructorKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ tag, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false, access, false });
    functions_.back().implicit = true;
    functions_.back().isNoexcept = implicitNoexcept(type, kImplicitDefault);
}

// **The destructor the class did not write**, which becomes a function exactly when a
// base or a member has one of its own to run - measured with cl. **A virtual function
// does not make it non-trivial**; a base whose destructor is virtual makes it virtual.
void Parser::declareImplicitDestructor(const std::string &tag, const Type *type,
                                       std::size_t pos) {
    if (overloadsOf(destructorKey(tag)) != nullptr) return;

    bool work = false;
    bool isVirtual = false;
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (const Signature *d = destructorOf(bs[i].type)) {
            work = true;
            if (d->isVirtual) isVirtual = true;
        }
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (destructorOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (nonPublicDefault(defaultedFor(tag).dtor)) work = true;
    if (!work) return;

    registerDestructor(tag, pos, Access::Public, isVirtual, true, VirtSpecifiers(), type);
}

// Its body: the members this class added, in the reverse of the order they
// were declared, then the bases in the reverse of theirs.
std::vector<StmtPtr> Parser::memberDestructors(const std::string &cls,
                                               const Type *type, int thisSlot,
                                               std::size_t pos) {
    std::vector<StmtPtr> out;
    // [class.union]/4: a union's destructor does not destroy its variant members - its body does, if anything.
    if (type->kind() == Kind::Union) return out;
    const std::vector<Member> &ms = type->members();
    for (std::size_t n = ms.size(); n-- > 0; )
        if (!memberFromBase(type, ms[n])) memberDestructor(out, cls, type, thisSlot, n, pos);
    return out;
}

void Parser::memberDestructor(std::vector<StmtPtr> &out, const std::string &cls, const Type *type,
                              int thisSlot, std::size_t n, std::size_t pos) {
    const std::vector<Member> &ms = type->members();
    const Type *mt = ms[n].type;
    long long elemCount = 1;
    const Type *elem = mt->isArray() ? memberElements(mt, &elemCount) : mt;
    const Signature *dtor = destructorOf(memberClass(mt));
    if (dtor == nullptr) return;
    if (dtor->access != Access::Public)
        src_.fail(pos, "'" + cls + "' cannot be destroyed by the destructor "
                       "the compiler would write: the destructor of '" +
                       memberClass(mt)->tag() + "', the type of '" +
                       ms[n].name + "', is " +
                       (dtor->access == Access::Private ? "private"
                                                        : "protected"));

    int indexSlot = 0;
    long long count = 0;
    if (mt->isArray()) {
        count = elemCount;
        if (count < 0)
            src_.fail(pos, "'" + cls + "::" + ms[n].name + "' has no length, "
                           "so the destructor the compiler would write does "
                           "not know how many elements to destroy");
        indexSlot = allocateFrameSlot(types_.intType());
    }

    ExprPtr acc = thisMember(thisSlot, type, ms[n]);

    ExprPtr address;
    if (mt->isArray()) {
        // **Backwards**, because an array is destroyed in the reverse of
        // the order it was built: the index counts up and the element it
        // reaches is (count - 1 - i).
        const Type *idx = types_.intType();
        ExprPtr last(new Num(count - 1));
        last->setType(idx);
        ExprPtr i(Var::local("$i", indexSlot));
        i->setType(idx);
        ExprPtr back(new Binary(BinOp::Sub, std::move(last), std::move(i)));
        back->setType(idx);
        ExprPtr size(new Num(static_cast<long long>(elem->size(target_))));
        size->setType(idx);
        ExprPtr off(new Binary(BinOp::Mul, std::move(back), std::move(size)));
        off->setType(idx);
        const Type *ptr = types_.pointerTo(elem->unqualified());
        ExprPtr at(new Binary(BinOp::Add, decay(std::move(acc)),
                              std::move(off)));
        at->setType(ptr);
        address = std::move(at);
    } else {
        address = ExprPtr(new Unary('&', std::move(acc)));
        address->setType(types_.pointerTo(elem->unqualified()));
    }

    StmtPtr one(new ExprStmt(destructorCall(std::move(address), *dtor, pos)));
    out.push_back(mt->isArray()
                   ? eachElement(indexSlot, count, std::move(one))
                   : std::move(one));
}

StmtPtr Parser::baseDestructorCall(const Type *cls, std::size_t which, int thisSlot,
                                   std::size_t pos) {
    const Type::BaseSpec &spec = cls->bases()[which];
    const Type *base = spec.type;
    const Signature *dtor = destructorOf(base);
    markUsed(dtor);
    std::string symbol = dtor->symbol;
    if (!target_.microsoftNames()) itaniumDestructorName(base->tag(), base, false, &symbol);
    const Type *basePtr = types_.pointerTo(base);
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(types_.pointerTo(cls));
    // A virtual base is destroyed this way only by the complete object's constructor, whose layout is fixed.
    if (spec.isVirtual && spec.offset != 0) {
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        ExprPtr asChars(new Cast(chars, std::move(me)));
        asChars->setType(chars);
        ExprPtr step(new Num(static_cast<long long>(spec.offset)));
        step->setType(types_.get(Kind::LongLong));
        me = ExprPtr(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
        me->setType(chars);
        me = ExprPtr(new Cast(basePtr, std::move(me)));
    } else {
        me = spec.offset == 0 ? ExprPtr(new Cast(basePtr, std::move(me)))
                              : convert(std::move(me), basePtr);
    }
    me->setType(basePtr);
    std::vector<ExprPtr> args;
    args.push_back(std::move(me));
    std::vector<const Type *> params;
    params.push_back(basePtr);
    if (takesVtt(base)) {
        args.push_back(spec.isVirtual ? vttOfClass(cls, vtts_[cls->tag()].virtualVtt[base])
                                      : vttForBase(cls, base));
        params.push_back(vttType());
    }
    return StmtPtr(new ExprStmt(completeCall(base->tag(), symbol, nullptr, types_.get(Kind::Void),
                                             params, false, pos, std::move(args), false, 0)));
}

// [except.ctor]/2: a delegating constructor's object is complete once its target returns.
// Its virtual bases go with it where it is the complete object; as a base they are the most derived class's, which
// is unwinding too. The flag says which on cl's ABI and the VTT on Itanium, as it did on the way in.
StmtPtr Parser::selfDestructorCall(const Type *cls, int thisSlot, std::size_t pos) {
    const Signature *dtor = destructorOf(cls);
    markUsed(dtor);
    const Type *self = types_.pointerTo(cls);
    const auto calling = [&](const std::string &symbol, bool withVtt) {
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> params(1, self);
        if (withVtt) {
            ExprPtr vtt(Var::local(".vtt", vttSlot_));
            vtt->setType(vttType());
            args.push_back(std::move(vtt));
            params.push_back(vttType());
        }
        return StmtPtr(new ExprStmt(completeCall(cls->tag(), symbol, nullptr, types_.get(Kind::Void),
                                                 params, false, pos, std::move(args), false, 0)));
    };
    std::string d2 = dtor->symbol;
    if (!target_.microsoftNames()) itaniumDestructorName(cls->tag(), cls, false, &d2);
    if (!cls->hasVirtualBase()) return calling(d2, false);
    if (target_.microsoftNames()) {
        ExprPtr complete(Var::local(".initVBases", msVbInitSlot_));
        complete->setType(types_.intType());
        return StmtPtr(new If(std::move(complete), calling(vbaseDestructorSymbol(cls->tag()), false), calling(d2, false)));
    }
    std::string d1;
    itaniumDestructorName(cls->tag(), cls, true, &d1);
    ExprPtr vtt(Var::local(".vtt", vttSlot_));
    vtt->setType(vttType());
    ExprPtr own(new Cast(vttType(), vttOfClass(cls, 0)));
    own->setType(vttType());
    ExprPtr complete(new Binary(BinOp::Eq, std::move(vtt), std::move(own)));
    complete->setType(types_.intType());
    return StmtPtr(new If(std::move(complete), calling(d1, false), calling(d2, true)));
}

StmtPtr Parser::guardConstruction(std::vector<StmtPtr> steps, std::vector<Alive> done,
                                  std::size_t pos, bool *guarded) {
    OwnPads own(*this);
    done.resize(steps.size());
    std::vector<StmtPtr> rest;
    for (std::size_t i = steps.size(); i-- > 0; ) {
        if (done[i].owner != nullptr && !rest.empty()) {
            Block *after = new Block(std::move(rest));
            after->setScope(-1);
            rest.clear();
            rest.push_back(guardSubobject(StmtPtr(after), done[i], pos));
            *guarded = true;
        }
        rest.insert(rest.begin(), std::move(steps[i]));
    }
    Block *b = new Block(std::move(rest));
    b->setScope(-1);
    return StmtPtr(b);
}

StmtPtr Parser::guardElements(StmtPtr loop, Alive a, int indexSlot, std::size_t pos) {
    if (destructorOf(a.cls) == nullptr) return loop;
    a.builtSlot = indexSlot;
    return guardSubobject(std::move(loop), a, pos);
}

StmtPtr Parser::withVirtualBases(std::vector<StmtPtr> init, const std::vector<Alive> &vbs, StmtPtr rest,
                                 std::size_t pos) {
    if (init.empty()) return rest;
    std::vector<StmtPtr> guarded;
    for (std::size_t i = vbs.empty() ? 0 : 1; i < init.size(); i++) guarded.push_back(std::move(init[i]));
    guarded.push_back(std::move(rest));
    std::vector<StmtPtr> all;
    if (!vbs.empty()) all.push_back(std::move(init[0]));
    all.push_back(guardSubobjects(StmtPtr(new Block(std::move(guarded))), vbs, pos));
    Block *b = new Block(std::move(all));
    b->setScope(-1);
    return StmtPtr(b);
}

StmtPtr Parser::guardSubobjects(StmtPtr rest, const std::vector<Alive> &as, std::size_t pos) {
    if (as.empty()) return rest;
    for (const Alive &a : as) alive_.push_back(a);
    std::vector<StmtPtr> one;
    one.push_back(std::move(rest));
    const std::vector<std::pair<std::size_t, std::size_t> > built(
        1, std::make_pair(std::size_t(0), alive_.size()));
    std::vector<StmtPtr> out = target_.microsoftNames()
        ? wrapMsCleanups(std::move(one), built, alive_.size() - as.size(), pos)
        : wrapCleanups(std::move(one), built, alive_.size() - as.size(), pos);
    alive_.resize(alive_.size() - as.size());
    Block *b = new Block(std::move(out));
    b->setScope(-1);
    return StmtPtr(b);
}

StmtPtr Parser::guardSubobject(StmtPtr rest, const Alive &a, std::size_t pos,
                               const std::vector<Alive> &before) {
    for (const Alive &b : before) alive_.push_back(b);
    alive_.push_back(a);
    std::vector<StmtPtr> one;
    one.push_back(std::move(rest));
    const std::vector<std::pair<std::size_t, std::size_t> > built(
        1, std::make_pair(std::size_t(0), alive_.size()));
    std::vector<StmtPtr> out = target_.microsoftNames()
        ? wrapMsCleanups(std::move(one), built, alive_.size() - 1, pos)
        : wrapCleanups(std::move(one), built, alive_.size() - 1, pos);
    alive_.resize(alive_.size() - 1 - before.size());
    Block *b = new Block(std::move(out));
    b->setScope(-1);
    return StmtPtr(b);
}

void Parser::synthesizeDestructor(std::size_t which) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const bool isVirtual = functions_[which].isVirtual;
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const FrameSave savedFrame = openFrame();
    const Type *self = types_.pointerTo(type);
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    const int savedVtt = vttSlot_;
    vttSlot_ = -1;
    if (takesVtt(type)) {
        vttSlot_ = allocateFrameSlot(vttType());
        params.push_back(Param{ vttType(), vttSlot_ });
    }

    std::vector<StmtPtr> body;

    const std::vector<Type::BaseSpec> &bs = type->bases();

    OwnPads own(*this);
    const bool chained = destructorMayThrow(type);
    if (chained) body.push_back(destroySubobjects(StmtPtr(), type, thisSlot, pos));

    // The members, then the bases below - [class.dtor]/8's order. The member
    // half is shared with the destructor the program writes, which is where it
    // was missing.
    if (!chained) {
        std::vector<StmtPtr> mine = memberDestructors(cls, type, thisSlot, pos);
        for (std::size_t i = 0; i < mine.size(); i++)
            body.push_back(std::move(mine[i]));
    }

    for (std::size_t n = bs.size(); !chained && n-- > 0; ) {
        const Type *base = bs[n].type;
        // D1 destroys the virtual bases, after this body has run - the mirror
        // of C1 building them before it. See synthesizeCompleteDtor.
        if (bs[n].isVirtual) continue;
        const Signature *dtor = destructorOf(base);
        if (dtor == nullptr) continue;
        // [class.protected]: a protected base destructor is the derived class's to call - a facet's is.
        if (dtor->access == Access::Private)
            src_.fail(pos, "'" + cls + "' cannot be destroyed by the destructor "
                           "the compiler would write: the destructor of its "
                           "base '" + base->tag() + "' is private");
        // **Calling one is what asks for a body**, and this is the only synthesiser that built its call by hand and forgot to say so.
        markUsed(dtor);
        // The base-subobject form, D2, which is what a derived class calls -
        // the same name a written destructor reaches for.
        std::string sym = dtor->symbol;
        if (!target_.microsoftNames())
            itaniumDestructorName(base->tag(), base, false, &sym);

        const Type *basePtr = types_.pointerTo(base);
        ExprPtr me(Var::local("this", thisSlot));
        if (bs[n].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = convert(std::move(me), basePtr);
        }
        std::vector<ExprPtr> args;
        args.push_back(target_.microsoftNames() ? msShifted(std::move(me), msThisAdjust(sym)) : std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        if (takesVtt(base)) {
            args.push_back(vttForBase(type, base));
            ps.push_back(vttType());
        }
        body.push_back(StmtPtr(new ExprStmt(
            completeCall("~" + base->tag(), sym, nullptr, types_.get(Kind::Void),
                         ps, false, pos, std::move(args)))));
    }
    vttSlot_ = savedVtt;
    msEnterAdjusted(body, thisSlot, self, symbol);

    current_->functions.push_back(Function(cls + "::~" + localOf(cls),
                                           types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    current_->functions.back().setInline(true);
    current_->functions.back().setHasLandingPads(own.made());
    showMemberAs(current_->functions.back(), type, "~" + localOf(cls));
    // The Microsoft wrapper: `??1` above stops at this class's own part, and
    // `??_D` destroys it and then the virtual bases.
    if (target_.microsoftNames() && type->hasVirtualBase()) {
        closeFrame(savedFrame);
        synthesizeCompleteDtor(type, vbaseDestructorSymbol(cls), symbol, true,
                               pos);
    }
    if (!target_.microsoftNames()) {
        std::string d2;
        itaniumDestructorName(cls, type, false, &d2);
        if (type->hasVirtualBase()) {
            current_->functions.back().setSymbol(d2);
            closeFrame(savedFrame);
            synthesizeCompleteDtor(type, symbol, d2, true, pos);
        } else {
            current_->functions.back().setAlias(d2);
        }
    }
    closeFrame(savedFrame);

    // A virtual one puts its deleting form in the vtable, as a written one.
    if (isVirtual) synthesizeDeleting(cls, type, Access::Public, pos);
}

// The copy assignment operator the class did not write. The trivial line is drawn
// where the others are and measured the same way with cl. A polymorphic class is
// non-trivial even though the body leaves the vptr alone: it writes into its own.
void Parser::declareImplicitCopyAssign(const std::string &tag, const Type *type,
                                       std::size_t pos) {
    const Defaulted df = defaultedFor(tag);
    // copyAssignOf answers a move where no copy is written: only a copy written here ends this - a copy
    // defaulted beside a written move assignment is declared (1.82: it was not, and `a = b` of an lvalue was
    // refused as binding to the move's `V &&`).
    if (const Signature *had = copyAssignOf(type))
        if (!(df.copyAssign.is && had->params.size() == 1 && had->params[0]->isRValueReference())) return;
    // [class.copy]/18: a move written `= default` deletes the copy - declared, so a use is refused by
    // name. **Not one the class wrote `= default` itself** (2026-10-21, review of 1.25, fix 4): /18
    // deletes the *implicitly-declared* one, and a defaulted one is user-declared.
    bool deleted = (df.moveCtor.is || df.moveAssign.is) && !df.copyAssign.is;
    // [class.copy]/23.
    if (moveConstructorOf(type) != nullptr && !deleted && !df.copyAssign.is) return;

    // **A const member has no assignment to give**, so the operator the compiler would
    // write is deleted rather than non-trivial and none is declared. Asked over every
    // member first: the search below stops at the first that gives it work.
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++)
        if (ms[i].type->isConst()) return;

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (deletedSpecial(copyAssignOf(bs[i].type))) deleted = true;
    for (std::size_t i = 0; i < ms.size(); i++)
        if (deletedSpecial(copyAssignOf(memberClass(ms[i].type)))) deleted = true;
    // A defaulted copy assignment beside a defaulted move assignment is declared even where it is trivial, as
    // the constructors are: the move alone would take every call, and `a = b` of an lvalue was refused (1.77).
    bool work = type->polymorphic() || deleted || nonPublicDefault(df.copyAssign) ||
                (df.copyAssign.is && (df.moveAssign.is || moveAssignOf(type) != nullptr));
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (copyAssignOf(bs[i].type) != nullptr) work = true;
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (copyAssignOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (!work) return;

    std::vector<const Type *> params;
    params.push_back(types_.referenceTo(types_.withConst(type)));
    const Type *self = types_.referenceTo(type);
    const Type *fn = types_.functionType(self, params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftCopyAssignName(tag, type, fn, 'Q', &out, &why)
            : itaniumCopyAssignName(tag, type, fn, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a copy assignment the compiler would "
                       "write, and it cannot be given a name the linker can "
                       "hold: " + why);

    functionIndex_[assignmentKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ "operator=", out, self, params, false, false,
                                    pos, false, tag, false, Access::Public,
                                    false });
    functions_.back().implicit = true;
    functions_.back().isNoexcept = implicitNoexcept(type, kImplicitCopyAssign);
    functions_.back().deleted = deleted;
}

// The move assignment the class did not write - [class.copy]/20: declared where
// none of /9's five is user-declared; one /23 would delete is left undeclared
// (DR 1402), so the copy assignment takes the call, as with a const member.
void Parser::declareImplicitMoveAssign(const std::string &tag, const Type *type, std::size_t pos,
                                       bool userDeclared) {
    if (userDeclared) return;
    if (moveAssignOf(type) != nullptr) return;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++)
        if (ms[i].type->isConst()) return;
    // What each subobject's move would call: its move assignment, or its copy where it has none.
    bool work = type->polymorphic();
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Signature *op = moveAssignOf(bs[i].type);
        if (op == nullptr) op = copyAssignOf(bs[i].type);
        if (deletedSpecial(op)) return;
        if (op != nullptr) work = true;
    }
    for (std::size_t i = 0; i < ms.size(); i++) {
        const Signature *op = moveAssignOf(memberClass(ms[i].type));
        if (op == nullptr) op = copyAssignOf(memberClass(ms[i].type));
        if (deletedSpecial(op)) return;
        if (op != nullptr) work = true;
    }
    if (!work) return;
    declareDefaultedMoveAssign(tag, type, pos);
    functions_.back().isNoexcept = implicitNoexcept(type, kImplicitMoveAssign);
}

// `X &operator=(X &&) = default;` - the move assignment the compiler writes: each subobject's own
// move assignment, or its copy where it has none ([class.copy]/28).
void Parser::declareDefaultedMoveAssign(const std::string &tag, const Type *type, std::size_t pos) {
    std::vector<const Type *> params;
    params.push_back(types_.rvalueReferenceTo(type));
    const Type *self = types_.referenceTo(type);
    const Type *fn = types_.functionType(self, params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftCopyAssignName(tag, type, fn, 'Q', &out, &why)
            : itaniumCopyAssignName(tag, type, fn, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a move assignment the compiler would write, and it "
                       "cannot be given a name the linker can hold: " + why);
    functionIndex_[assignmentKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ "operator=", out, self, params, false, false,
                                    pos, false, tag, false, Access::Public, false });
    functions_.back().implicit = true;
}

// The copy constructor the class did not write, on the same measured line: a class
// writing any constructor still gets one and only writing a copy takes it away. The
// implicit *move* needs all five of [class.copy]/9's absences, two of them vacuous.
void Parser::declareImplicitMoveCtor(const std::string &tag, const Type *type,
                                     std::size_t pos, bool userDeclared) {
    if (userDeclared) return;
    if (moveConstructorOf(type) != nullptr) return;

    // A defaulted move is declared even where it is trivial: the copy it deletes would otherwise take the call.
    bool work = type->polymorphic() || defaultedFor(tag).moveCtor.is;
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (moveConstructorOf(bs[i].type) != nullptr ||
            copyConstructorOf(bs[i].type) != nullptr) work = true;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++) {
        const Type *mc = memberClass(ms[i].type);
        if (moveConstructorOf(mc) != nullptr ||
            copyConstructorOf(mc) != nullptr) work = true;
    }
    if (!work) return;

    std::vector<const Type *> params;
    // Not const, and that is the whole point of it: the source is going to be
    // taken apart, so the member moves below need to be able to write to it.
    params.push_back(types_.rvalueReferenceTo(type));
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a move constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    functionIndex_[constructorKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ tag, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false,
                                    Access::Public, false });
    functions_.back().implicit = true;
    functions_.back().isNoexcept = implicitNoexcept(type, kImplicitMove);
}

void Parser::declareImplicitCopyCtor(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (copyConstructorOf(type) != nullptr) return;
    const Defaulted df = defaultedFor(tag);
    // [class.copy]/7: a user-declared move deletes the implicit copy (written
    // by hand: left undeclared, refused where the move is not viable); a copy
    // written `= default` beside it is user-declared, not deleted.
    bool deleted = (df.moveCtor.is || df.moveAssign.is) && !df.copyCtor.is;
    if (moveConstructorOf(type) != nullptr && !deleted && !df.copyCtor.is) return;
    // A move assignment the class wrote deletes it as well, and it is declared so - a class with only that
    // was copied by its bytes, `Q b(a);` (1.79).
    if (const Signature *written = moveAssignOf(type))
        if (!written->implicit && !df.copyCtor.is) deleted = true;

    // [class.copy]/11: a base or member whose copy is deleted deletes this one.
    const std::vector<Type::BaseSpec> &bs = type->bases();
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (deletedSpecial(copyConstructorOf(bs[i].type))) deleted = true;
    for (std::size_t i = 0; i < ms.size(); i++)
        if (deletedSpecial(copyConstructorOf(memberClass(ms[i].type)))) deleted = true;
    // A defaulted copy beside a move is declared even where it is trivial, as the move is: an
    // lvalue call would otherwise find the move alone and have nothing viable.
    bool work = type->polymorphic() || deleted || nonPublicDefault(df.copyCtor) ||
                (df.copyCtor.is && (df.moveCtor.is || moveConstructorOf(type) != nullptr));
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (copyConstructorOf(bs[i].type) != nullptr) work = true;
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (copyConstructorOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (!work) return;

    std::vector<const Type *> params;
    params.push_back(types_.referenceTo(types_.withConst(type)));
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a copy constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    functionIndex_[constructorKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ tag, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false,
                                    Access::Public, false });
    functions_.back().implicit = true;
    functions_.back().isNoexcept = implicitNoexcept(type, kImplicitCopy);
    functions_.back().deleted = deleted;
}

// `using B::m;` in `tag`'s body - [namespace.udecl]/3: m, a member of base B,
// becomes this class's too, with the access it is written under (/15); its
// overloads join once the class is complete, other members keep their entity.
void Parser::classUsingDeclaration(const std::string &tag, const Type *type, Access access) {
    const std::size_t pos = peek().pos;
    at_++;
    consume("typename");
    std::size_t end = at_, cut = 0;
    for (int depth = 0; end < tokens_.size() && !(depth == 0 && tokens_[end].is(";")); end++) {
        if (tokens_[end].kind == TokenKind::End) src_.fail(pos, "this using-declaration never ends");
        if (depth > 0 && (tokens_[end].is("(") || tokens_[end].is("["))) { end = groupCloseAt(end); continue; }
        if (tokens_[end].is("<")) depth++;
        else if (tokens_[end].is(">")) depth--;
        else if (tokens_[end].is(">>")) depth -= 2;
        else if (depth == 0 && tokens_[end].is("::") && !tokens_[end - 1].is("operator")) cut = end;
    }
    if (cut <= at_)
        src_.fail(pos, "a using-declaration in a class names a member of one of its bases, 'using B::m;' - "
                       "[namespace.udecl]/3");
    std::string name;
    if (tokens_[cut + 1].is("operator")) {
        for (std::size_t k = cut + 2; k < end; k++)
            name += (tokens_[k].kind == TokenKind::Str ? "\"\" " + tokens_[k].udSuffix : tokens_[k].text);
        name = "operator" + name;
    } else {
        if (tokens_[cut + 1].kind != TokenKind::Ident || cut + 2 != end)
            src_.fail(tokens_[cut + 1].pos, "expected the name of a member of a base after '::'");
        name = tokens_[cut + 1].text;
    }
    std::size_t q = cut - 1;
    if (tokens_[q].is(">")) {
        for (int depth = 0; q > at_; q--) {
            if (tokens_[q].is(">")) depth++;
            else if (tokens_[q].is("<") && --depth == 0) break;
        }
        q--;
    }
    const std::string named = tokens_[q].text;
    const Type *base = nullptr;
    for (const Type::BaseSpec &b : type->bases()) {
        const Type *bt = b.type->unqualified();
        if (bt->localName() == named || bt->tag() == named ||
            (bt->isSpecialization() && bt->templateName() == named))
            base = bt;
    }
    if (base == nullptr)
        src_.fail(tokens_[q].pos, "'" + named + "' is not a base of '" + tag + "', and a using-declaration "
                                  "in a class names a member of a base - [namespace.udecl]/3");
    at_ = end + 1;
    const std::string key = tag + "::" + name;
    if (findMemberOwner(base, name) != nullptr || isMemberTemplate(base, name)) {
        classUsings_[tag].push_back(ClassUsing{ name, base, pos });
        usingAccess_[key] = access;
        return;
    }
    if (base->findMember(name) != nullptr || base->findStaticMember(name) != nullptr) {
        usingAccess_[key] = access;
        return;
    }
    if (const Type *t = lookupInClass(base, name)) {
        typedefIndex_[key] = typedefs_.size();
        typedefs_.push_back(TypedefName{ key, t });
        return;
    }
    if (enumInClass(base, name) != nullptr) return;
    src_.fail(tokens_[cut + 1].pos, "'" + base->describe() + "' has no member named '" + name + "' for this "
                                    "using-declaration to name");
}

void Parser::checkOverride(const Type *type, const Signature &f, bool deleted, std::size_t pos) const {
    struct Derives {
        static bool from(const Type *t, const Type *base) {
            for (const Type::BaseSpec &b : t->bases())
                if (b.type->unqualified() == base || from(b.type->unqualified(), base)) return true;
            return false;
        }
    };
    auto allowsNone = [](const Signature &s) { return s.isNoexcept || (s.spec.written && s.spec.types.empty()); };
    auto allowsAll = [](const Signature &s) { return !s.isNoexcept && !s.spec.written; };
    for (const Type::BaseSpec &b : type->bases()) {
        const std::vector<std::size_t> *set = overloadsOf(b.type->unqualified()->tag() + "::" + f.name);
        if (set == nullptr) continue;
        for (std::size_t i : *set) {
            const Signature &g = functions_[i];
            if (!g.isVirtual || g.params != f.params || g.constThis != f.constThis ||
                g.volatileThis != f.volatileThis || g.refThis != f.refThis)
                continue;
            if (g.deleted != deleted)
                src_.fail(pos, std::string(deleted ? "deleted function '" : "non-deleted function '") + f.name +
                               "' cannot override a " + (deleted ? "non-deleted" : "deleted") + " function - "
                               "[class.virtual]/14");
            // [except.spec]/5: the overrider allows no exception the function it overrides does not. One whose
            // `noexcept(e)` waits for its class (CWG 1330) is asked when that is read - settleDeferredSpecs.
            bool waits = false;
            for (const DeferredSpec &d : deferredSpecs_)
                waits = waits || (d.function < functions_.size() && &functions_[d.function] == &f);
            if (waits) continue;
            if (f.name[0] == '~' || allowsAll(g) || allowsNone(f)) continue;
            bool looser = allowsAll(f) || allowsNone(g);
            for (std::size_t k = 0; k < f.spec.types.size() && !looser; k++) {
                const Type *t = f.spec.types[k];
                bool allowed = false;
                for (const Type *u : g.spec.types)
                    allowed = allowed || t == u ||
                              (t->isStructOrUnion() && u->isStructOrUnion() && Derives::from(t, u));
                looser = !allowed;
            }
            if (looser)
                src_.fail(pos, "'" + f.name + "' allows exceptions the virtual function it overrides does not - "
                               "[except.spec]/5: an overrider's exception specification is at least as strict");
        }
    }
}

const Type *Parser::usingAccessOf(const Type *cls, const Type *decl, const std::string &name, Access *out) const {
    if (usingAccess_.empty()) return nullptr;
    struct Derives {
        static bool from(const Type *t, const Type *base) {
            for (const Type::BaseSpec &b : t->bases())
                if (b.type->unqualified() == base || from(b.type->unqualified(), base)) return true;
            return false;
        }
    };
    for (const Type *c = cls != nullptr ? cls->unqualified() : nullptr; c != nullptr && c != decl;) {
        std::map<std::string, Access>::const_iterator u = usingAccess_.find(c->tag() + "::" + name);
        if (u != usingAccess_.end()) {
            *out = u->second;
            return c;
        }
        const Type *down = nullptr;
        for (const Type::BaseSpec &b : c->bases())
            if (b.direct && (b.type->unqualified() == decl || Derives::from(b.type->unqualified(), decl)))
                down = b.type->unqualified();
        c = down;
    }
    return nullptr;
}

// [namespace.udecl]/15: each function a using-declaration names joins the class's set for its name,
// except one a member of the class's own hides - the same name, parameter types, cv- and ref-qualifier.
void Parser::declareUsingMembers(const std::string &tag) {
    std::map<std::string, std::vector<ClassUsing> >::const_iterator it = classUsings_.find(tag);
    if (it == classUsings_.end()) return;
    for (const ClassUsing &u : it->second) {
        const Type *owner = findMemberOwner(u.base, u.name);
        if (owner == nullptr) continue;
        const std::vector<std::size_t> from = *overloadsOf(owner->tag() + "::" + u.name);
        std::vector<std::size_t> &set = functionIndex_[tag + "::" + u.name];
        const std::vector<std::size_t> own = set;
        for (std::size_t idx : from) {
            const Signature &b = functions_[idx];
            bool hidden = std::find(set.begin(), set.end(), idx) != set.end();
            for (std::size_t o : own) {
                const Signature &d = functions_[o];
                if (d.owner == tag && d.params == b.params && d.constThis == b.constThis &&
                    d.volatileThis == b.volatileThis && d.refThis == b.refThis)
                    hidden = true;
            }
            if (!hidden) set.push_back(idx);
        }
    }
}

// **`using B::B;`**: each constructor of B but its default, copy and move ones becomes one of this
// class's, with B's parameters, defaults, access and `explicit`, building B from them and every other
// base and member as the implicit default constructor would - [class.inhctor] as P0136 has it.
void Parser::declareInheritingCtors(const std::string &tag, const Type *type, std::size_t pos) {
    auto it = inheritFrom_.find(tag);
    if (it == inheritFrom_.end()) return;
    const std::vector<const Type *> bases = it->second;
    for (const Type *base : bases) {
        const std::vector<std::size_t> *set = overloadsOf(constructorKey(base->tag()));
        if (set == nullptr) continue;
        const std::vector<std::size_t> from = *set;
        for (std::size_t idx : from) {
            const Signature b = functions_[idx];
            if (b.deleted || b.owner != base->tag()) continue;
            // An inherited default constructor is D's own default one, by name and body, as clang has it.
            if (b.params.empty()) {
                bool has = false;
                if (const std::vector<std::size_t> *own = overloadsOf(constructorKey(tag)))
                    for (std::size_t k : *own)
                        if (functions_[k].params.empty()) has = true;
                if (has) continue;
                const Type *fn0 = types_.functionType(types_.get(Kind::Void), std::vector<const Type *>(), false);
                const char code0 = b.access == Access::Public ? 'Q' : b.access == Access::Protected ? 'I' : 'A';
                std::string out0, why0;
                const bool ok0 = target_.microsoftNames()
                        ? microsoftConstructorName(tag, type, fn0, code0, &out0, &why0)
                        : itaniumConstructorName(tag, type, fn0, true, &out0, &why0);
                if (!ok0)
                    src_.fail(pos, "'" + tag + "''s default constructor cannot be given a name the linker "
                                   "can hold: " + why0);
                functionIndex_[constructorKey(tag)].push_back(functions_.size());
                functions_.push_back(Signature{ tag, out0, types_.get(Kind::Void), std::vector<const Type *>(),
                                                false, false, pos, false, tag, false, b.access, false });
                functions_.back().implicit = true;
                continue;
            }
            if (b.params.size() == 1 && b.params[0]->isReference() &&
                b.params[0]->referent()->unqualified() == base->unqualified())
                continue;
            // One this class declared itself with the same parameters hides it.
            bool hidden = false;
            if (const std::vector<std::size_t> *own = overloadsOf(constructorKey(tag)))
                for (std::size_t k : *own)
                    if (sameParameters(functions_[k].params, b.params)) hidden = true;
            if (hidden) continue;
            if (b.variadic)
                src_.fail(pos, "inheriting '" + base->tag() + "''s constructor with '...' is not "
                               "supported yet - its arguments would have to be forwarded as they came");
            for (const Type *p : b.params)
                if (!p->isReference() && passedByAddress(p))
                    src_.fail(pos, "inheriting '" + base->tag() + "''s constructor that takes '" +
                                   p->describe() + "' by value is not supported yet - the copy "
                                   "would have to be passed on rather than made twice");
            const Type *fn = types_.functionType(types_.get(Kind::Void), b.params, false);
            auto chained = inheritedFrom_.find(b.symbol);
            const Type *origin = chained != inheritedFrom_.end() ? chained->second : base;
            const char code = b.access == Access::Public ? 'Q' : b.access == Access::Protected ? 'I' : 'A';
            std::string out, why;
            const bool ok = target_.microsoftNames()
                    ? microsoftConstructorName(tag, type, fn, code, &out, &why)
                    : itaniumInheritingConstructorName(tag, type, origin, fn, true, &out, &why);
            if (!ok)
                src_.fail(pos, "'" + tag + "''s constructor inherited from '" + base->tag() +
                               "' cannot be given a name the linker can hold: " + why);
            functionIndex_[constructorKey(tag)].push_back(functions_.size());
            functions_.push_back(Signature{ tag, out, types_.get(Kind::Void), b.params,
                                            false, false, pos, false, tag, false, b.access, false });
            functions_.back().implicit = true;
            functions_.back().isExplicit = b.isExplicit;
            auto dflt = defaultArgs_.find(b.symbol);
            if (dflt != defaultArgs_.end()) {
                defaultArgs_[out] = dflt->second;
                auto ns = defaultArgNamespace_.find(b.symbol);
                if (ns != defaultArgNamespace_.end()) defaultArgNamespace_[out] = ns->second;
            }
            inheriting_[functions_.size() - 1] = Inheriting{ base, idx };
            inheritedFrom_[out] = origin;
        }
    }
}

// The body of a default constructor nobody wrote: the bases in the order they were
// written, then the vptrs, then the members with constructors of their own. Scalars
// are left alone, as [dcl.init] has it; an inheriting constructor's forwards its parameters to its base.
void Parser::synthesizeDefaultCtor(std::size_t which) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const FrameSave savedFrame = openFrame();
    OwnPads own(*this);
    const Type *self = types_.pointerTo(type);
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    // Itanium's VTT second, for the same reason a written constructor takes
    // one; cl's hidden most-derived flag last, likewise.
    const int savedVtt = vttSlot_;
    vttSlot_ = -1;
    if (takesVtt(type)) {
        vttSlot_ = allocateFrameSlot(vttType());
        params.push_back(Param{ vttType(), vttSlot_ });
    }
    // An inheriting constructor's own parameters, B's: a reference arrives as its address.
    const auto inh = inheriting_.find(which);
    const Type *inhBase = inh != inheriting_.end() ? inh->second.base : nullptr;
    const std::vector<const Type *> forwarded = functions_[which].params;
    std::vector<int> fwdSlots;
    for (const Type *p : forwarded) {
        const Type *slotType = p->isReference() ? types_.pointerTo(p->referent()) : p;
        fwdSlots.push_back(allocateFrameSlot(slotType));
        params.push_back(Param{ slotType, fwdSlots.back() });
    }
    int flagSlot = -1;
    if (target_.microsoftNames() && type->hasVirtualBase()) {
        flagSlot = allocateFrameSlot(types_.intType());
        params.push_back(Param{ types_.intType(), flagSlot });
    }

    std::vector<StmtPtr> body;
    // What each statement of `body` finishes building, for [except.ctor]/3.
    std::vector<Alive> done;
    // cl's virtual bases, built under the most-derived flag and guarded around the rest, [except.ctor]/3.
    std::vector<Alive> vbs;
    std::vector<StmtPtr> vbInit =
        guardedVirtualBaseInit(type, cls, thisSlot, flagSlot, pos, -1, false, nullptr, &vbs);

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *base = bs[i].type;
        if (base->tag().empty()) continue;
        // **A virtual base belongs to C1, not to this body.** Two things go
        // wrong when it is built here.
        if (bs[i].isVirtual) continue;
        if (!hasConstructors(base->tag())) continue;
        const bool inherited = inhBase != nullptr && base->unqualified() == inhBase->unqualified();
        const Signature *ctor = inherited ? &functions_[inh->second.from] : defaultConstructorOf(base);
        if (ctor == nullptr)
            src_.fail(pos, "'" + cls + "' has no constructor of its own, and the "
                           "one the compiler would write cannot build its base '" +
                           base->tag() + "', which has no constructor taking "
                           "nothing - write a constructor for '" + cls + "' with "
                           "': " + base->tag() + "(...)' in its initialiser list");
        if (!accessibleFrom(type, base, ctor->access))
            src_.fail(pos, "'" + cls + "' cannot be built by the constructor the "
                           "compiler would write: the constructor of its base '" +
                           base->tag() + "' taking nothing is " +
                           (ctor->access == Access::Private ? "private"
                                                            : "protected"));
        markUsed(ctor);
        // Copied before the defaults are read - see constructLocalArray.
        const Signature chosen = *ctor;
        std::vector<ExprPtr> defaults;
        if (inherited) {
            // Forwarded as they came: an lvalue reference as the object, an rvalue one as an xvalue.
            for (std::size_t k = 0; k < forwarded.size(); k++) {
                const Type *p = forwarded[k];
                ExprPtr v(Var::local("$inh" + std::to_string(k), fwdSlots[k]));
                if (p->isReference()) {
                    v->setType(types_.pointerTo(p->referent()));
                    ExprPtr obj(new Unary('*', std::move(v)));
                    obj->setType(p->referent());
                    if (p->kind() == Kind::RValueRef) obj->setXvalue();
                    defaults.push_back(std::move(obj));
                } else {
                    v->setType(p);
                    defaults.push_back(std::move(v));
                }
            }
        } else {
            applyDefaults(chosen, defaults, pos);
        }
        const std::string sym = baseConstructorSymbol(chosen, base);

        const Type *basePtr = types_.pointerTo(base);
        ExprPtr me(Var::local("this", thisSlot));
        if (bs[i].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = convert(std::move(me), basePtr);
        }
        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        if (takesVtt(base)) {
            args.push_back(vttForBase(type, base));
            ps.push_back(vttType());
        }
        for (std::size_t k = 0; k < defaults.size(); k++) {
            args.push_back(std::move(defaults[k]));
            ps.push_back(chosen.params[k]);
        }
        // A base subobject, so cl's flag is 0: its virtual bases are this
        // class's to build, and it built them above.
        body.push_back(StmtPtr(new ExprStmt(
            completeCall(base->tag(), sym, nullptr, types_.get(Kind::Void), ps,
                         false, pos, std::move(args), false, 0))));
        done.resize(body.size());
        if (destructorOf(base) != nullptr) {
            done.back() = Alive{ "this", thisSlot, type };
            done.back().owner = type;
            done.back().base = static_cast<int>(i);
        }
    }

    // The vptr is written for either reason it exists.
    if (type->hasVptr()) {
        std::vector<StmtPtr> vp = storeVptrs(cls, type, thisSlot);
        for (std::size_t i = 0; i < vp.size(); i++)
            body.push_back(std::move(vp[i]));
    }

    // The members, in declaration order, each by the one rule that applies to it: the
    // initialiser the class wrote on it, or else its own default constructor. **One
    // walk, not two** - `M m = M(2);` used to be stored and then built over.
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        // **A base's members are this class's list too**, the layout having
        // copied them down - and the base's own constructor has already built
        // them by the time this body runs.
        if (memberFromBase(type, ms[i])) continue;
        StmtPtr one = memberInitialiser(cls, type, ms[i], thisSlot, pos);
        std::vector<ExprPtr> none;
        if (one == nullptr && type->kind() != Kind::Union && ms[i].anonUnion == nullptr)
            one = constructMember(cls, type, ms[i], thisSlot, none, pos, true);
        if (one == nullptr) continue;
        body.push_back(std::move(one));
        done.resize(body.size());
        if (memberHasDestructor(ms[i].type)) {
            done.back() = Alive{ "this", thisSlot, type };
            done.back().owner = type;
            done.back().member = static_cast<int>(i);
        }
    }

    bool guarded = false;
    StmtPtr built = withVirtualBases(std::move(vbInit), vbs,
                                     guardConstruction(std::move(body), std::move(done), pos, &guarded),
                                     pos);
    current_->functions.push_back(Function(cls + "::" + cls, types_.get(Kind::Void),
                                           std::move(params), std::move(built),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    current_->functions.back().setHasLandingPads(guarded || own.made());
    showMemberAs(current_->functions.back(), type, localOf(cls));
    current_->functions.back().setInline(true);
    // The same two names a written constructor is emitted under: C1 for a
    // complete object and C2 for a base subobject, the second a label in front
    // of the first. Microsoft has one name and wants no alias.
    if (!target_.microsoftNames() && inhBase != nullptr) {
        const Type *fnType = types_.functionType(types_.get(Kind::Void), forwarded, false);
        std::string c2, why;
        auto origin = inheritedFrom_.find(symbol);
        if (itaniumInheritingConstructorName(cls, type, origin != inheritedFrom_.end() ? origin->second : inhBase,
                                             fnType, false, &c2, &why)) {
            // A virtual base of the class splits it as it splits any constructor: this body is CI2.
            if (type->hasVirtualBase()) {
                current_->functions.back().setSymbol(c2);
                closeFrame(savedFrame);
                vttSlot_ = savedVtt;
                synthesizeCompleteCtor(type, forwarded, symbol, c2, true, pos);
                return;
            }
            current_->functions.back().setAlias(c2);
        }
    } else if (!target_.microsoftNames()) {
        const Type *fnType = types_.functionType(types_.get(Kind::Void),
                                                 std::vector<const Type *>(), false);
        std::string c2, why;
        if (itaniumConstructorName(cls, type, fnType, false, &c2, &why)) {
            // **A virtual base splits this the same way it splits a written
            // constructor.** The walk above skipped them, so this body is C2;
            // C1 builds them and calls it.
            if (type->hasVirtualBase()) {
                current_->functions.back().setSymbol(c2);
                closeFrame(savedFrame);
                vttSlot_ = savedVtt;
                synthesizeCompleteCtor(type, std::vector<const Type *>(),
                                       symbol, c2, true, pos);
                return;
            }
            current_->functions.back().setAlias(c2);
        }
    }
    closeFrame(savedFrame);
    vttSlot_ = savedVtt;
}

StmtPtr Parser::eachElement(int indexSlot, long long count, StmtPtr one) {
    ExprPtr n(new Num(count));
    n->setType(types_.intType());
    return eachElement(indexSlot, std::move(n), std::move(one));
}

StmtPtr Parser::eachElement(int indexSlot, ExprPtr n, StmtPtr one) {
    const Type *idx = types_.intType();

    ExprPtr i0(Var::local("$i", indexSlot));
    i0->setType(idx);
    ExprPtr zero(new Num(0LL));
    zero->setType(idx);
    ExprPtr init(new Assign(std::move(i0), std::move(zero)));
    init->setType(idx);

    ExprPtr i1(Var::local("$i", indexSlot));
    i1->setType(idx);
    ExprPtr cond(new Binary(BinOp::Lt, std::move(i1), std::move(n)));
    cond->setType(idx);

    ExprPtr i2(Var::local("$i", indexSlot));
    i2->setType(idx);
    ExprPtr step1(new Num(1LL));
    step1->setType(idx);
    ExprPtr sum(new Binary(BinOp::Add, std::move(i2), std::move(step1)));
    sum->setType(idx);
    ExprPtr i3(Var::local("$i", indexSlot));
    i3->setType(idx);
    ExprPtr step(new Assign(std::move(i3), std::move(sum)));
    step->setType(idx);

    std::vector<StmtPtr> inner;
    inner.push_back(std::move(one));
    inner.push_back(StmtPtr(new ExprStmt(std::move(step))));

    std::vector<StmtPtr> all;
    all.push_back(StmtPtr(new ExprStmt(std::move(init))));
    all.push_back(StmtPtr(new While(std::move(cond),
                                    StmtPtr(new Block(std::move(inner))))));
    return StmtPtr(new Block(std::move(all)));
}


// The body of a copy constructor nobody wrote: the bases with one of their own, then
// the vptrs, then every member no base copied. **The vptr is set and not copied** -
// measured in cl's listing. Members go one at a time, for the tail-padding rule.
void Parser::synthesizeCopy(std::size_t which, bool assigning) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const Type *srcRef = functions_[which].params[0];
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    // **The signature says which of the three this is**, so nothing that calls
    // this had to learn about moves: an implicit constructor whose parameter
    // is `X &&` is the move constructor and there is nothing else it could be.
    const bool moving = srcRef->isRValueReference();
    const char *kind = assigning ? (moving ? "move assignment" : "copy assignment")
                     : moving    ? "move constructor"
                                 : "copy constructor";

    const FrameSave savedFrame = openFrame();
    OwnPads own(*this);
    const Type *self = types_.pointerTo(type);
    const Type *srcPtr = types_.pointerTo(srcRef->referent());
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    const int thatSlot = allocateFrameSlot(srcPtr);
    params.push_back(Param{ self, thisSlot });
    // The VTT and cl's most-derived flag, for a *constructor* of such a
    // class: assignment takes neither, having no virtual bases to build.
    const int savedVtt = vttSlot_;
    vttSlot_ = -1;
    if (!assigning && takesVtt(type)) {
        vttSlot_ = allocateFrameSlot(vttType());
        params.push_back(Param{ vttType(), vttSlot_ });
    }
    params.push_back(Param{ srcPtr, thatSlot });
    int flagSlot = -1;
    if (!assigning && target_.microsoftNames() && type->hasVirtualBase()) {
        flagSlot = allocateFrameSlot(types_.intType());
        params.push_back(Param{ types_.intType(), flagSlot });
    }

    std::vector<StmtPtr> body;
    // What each statement of `body` finishes building - a constructor's, for [except.ctor]/3.
    std::vector<Alive> finished;
    std::vector<Alive> vbs;
    std::vector<StmtPtr> vbInit;
    {
        // **Copied out of the source's own virtual base**, which is what the
        // Itanium C1 does with the argument forwarded to it.
        vbInit = guardedVirtualBaseInit(type, cls, thisSlot, flagSlot, pos,
                                        thatSlot, moving, nullptr, assigning ? nullptr : &vbs);
    }

    // What a base's own copy constructor has already dealt with.
    std::vector<std::pair<int, int> > taken;

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *base = bs[i].type;
        // **A virtual base is copied by C1**, for the reason the default constructor records.
        if (bs[i].isVirtual && !assigning) continue;
        // **A member or base without a move constructor is copied, not refused.**
        // [class.copy]/15: the implicit move moves each subobject, and moving
        // something that has only a copy is what its copy constructor does.
        const Signature *cc = nullptr;
        if (assigning && moving) cc = moveAssignOf(base);
        if (assigning && cc == nullptr) cc = copyAssignOf(base);
        else if (!assigning) {
            if (moving) cc = moveConstructorOf(base);
            if (cc == nullptr) cc = copyConstructorOf(base);
        }
        if (cc == nullptr) continue;              // trivial: its members copy below
        if (!accessibleFrom(type, base, cc->access))
            src_.fail(pos, std::string("'") + cls + "' cannot be built by the " +
                           kind + " the compiler would write: the " + kind +
                           " of its base '" + base->tag() + "' is " +
                           (cc->access == Access::Private ? "private" : "protected"));
        markUsed(cc);
        const std::string sym = assigning ? cc->symbol
                                          : baseConstructorSymbol(*cc, base);
        const Type *basePtr = types_.pointerTo(base);

        ExprPtr me(Var::local("this", thisSlot));
        if (bs[i].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = convert(std::move(me), basePtr);
        }
        ExprPtr from(Var::local("that", thatSlot));
        from->setType(srcPtr);
        from = convert(std::move(from),
                       types_.pointerTo(moving ? base : types_.withConst(base)));
        ExprPtr fromObj(new Unary('*', std::move(from)));
        fromObj->setType(base);
        if (moving) fromObj->setXvalue();

        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        if (!assigning && takesVtt(base)) {
            args.push_back(vttForBase(type, base));
            ps.push_back(vttType());
        }
        args.push_back(std::move(fromObj));
        ps.push_back(cc->params[0]);
        body.push_back(StmtPtr(new ExprStmt(
            completeCall(base->tag(), sym, nullptr, cc->returns, ps,
                         false, pos, std::move(args), false,
                         assigning ? 1 : 0))));
        finished.resize(body.size());
        if (!assigning && destructorOf(base) != nullptr) {
            finished.back() = Alive{ "this", thisSlot, type };
            finished.back().owner = type;
            finished.back().base = static_cast<int>(i);
        }
        taken.push_back(std::make_pair(bs[i].offset,
                                       bs[i].offset + base->dataSize()));
    }

    // **A copy constructor sets the vptr; a copy assignment leaves it alone.** That is
    // the whole difference between the two bodies, measured in cl's own listing:
    // assignment writes into an object that already exists and is already this class.
    if (!assigning && type->polymorphic()) {
        std::vector<StmtPtr> vp = storeVptrs(cls, type, thisSlot);
        for (std::size_t i = 0; i < vp.size(); i++)
            body.push_back(std::move(vp[i]));
    }

    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        bool done = false;
        for (std::size_t k = 0; k < taken.size() && !done; k++)
            if (ms[i].offset >= taken[k].first && ms[i].offset < taken[k].second)
                done = true;
        if (done) continue;

        const Type *mt = ms[i].type;
        long long elemCount = 1;
        const Type *elem = mt->isArray() ? memberElements(mt, &elemCount) : mt;
        const Signature *cc = nullptr;
        if (assigning && moving) cc = moveAssignOf(memberClass(mt));
        if (assigning && cc == nullptr) cc = copyAssignOf(memberClass(mt));
        else if (!assigning) {
            // [class.copy]/15: the member is initialised from `static_cast<M &&>(x.m)`, which for a const M
            // is `const M &&` - the copy constructor's, never the move's: pair<const string, int> moves its
            // second and copies its first.
            if (moving && !elem->isConst() && !elem->isVolatile()) cc = moveConstructorOf(memberClass(mt));
            if (cc == nullptr) cc = copyConstructorOf(memberClass(mt));
        }
        if (cc != nullptr) {
            if (cc->access != Access::Public)
                src_.fail(pos, std::string("'") + cls + "' cannot be built by "
                               "the " + kind + " the compiler would write: the " +
                               kind + " of '" + memberClass(mt)->tag() +
                               "', the type of '" + ms[i].name + "', is " +
                               (cc->access == Access::Private ? "private"
                                                              : "protected"));
            markUsed(cc);
        }

        int indexSlot = 0;
        long long count = 0;
        if (mt->isArray()) {
            count = elemCount;
            if (count < 0)
                src_.fail(pos, "'" + cls + "::" + ms[i].name + "' has no length, "
                               "so the copy constructor the compiler would write "
                               "does not know how much to copy");
            indexSlot = allocateFrameSlot(types_.intType());
        }

        // Both sides of the copy, as lvalues.
        ExprPtr dst = thisMember(thisSlot, type, ms[i]);

        ExprPtr from(Var::local("that", thatSlot));
        from->setType(srcPtr);
        ExprPtr fromObj(new Unary('*', std::move(from)));
        fromObj->setType(srcRef->referent());
        if (moving) fromObj->setXvalue();
        ExprPtr src(new MemberAccess(std::move(fromObj), ms[i].name, ms[i].offset,
                                     ms[i].width, ms[i].bitOffset));
        src->setType(mt);

        if (mt->isArray()) {
            ExprPtr dstAt = indexBytes(types_, decay(std::move(dst)), elem,
                                       indexSlot, target_);
            ExprPtr srcAt = indexBytes(types_, decay(std::move(src)), elem,
                                       indexSlot, target_);
            dst = ExprPtr(new Unary('*', std::move(dstAt)));
            dst->setType(elem);
            src = ExprPtr(new Unary('*', std::move(srcAt)));
            src->setType(elem);
        }

        // After the array unwrap, so that it lands on the element actually handed over.
        if (moving) src->setXvalue();

        StmtPtr one;
        if (cc != nullptr) {
            ExprPtr addr(new Unary('&', std::move(dst)));
            addr->setType(types_.pointerTo(elem->unqualified()));
            std::vector<ExprPtr> args;
            args.push_back(std::move(addr));
            args.push_back(std::move(src));
            std::vector<const Type *> ps;
            ps.push_back(types_.pointerTo(elem->unqualified()));
            ps.push_back(cc->params[0]);
            one = StmtPtr(new ExprStmt(
                completeCall(elem->unqualified()->tag(), cc->symbol, nullptr,
                             cc->returns, ps, false, pos, std::move(args))));
        } else {
            ExprPtr store(new Assign(std::move(dst), std::move(src)));
            store->setType(elem);
            one = StmtPtr(new ExprStmt(std::move(store)));
        }

        if (mt->isArray()) {
            Alive built{ "this", thisSlot, elem->unqualified() };
            built.owner = type;
            built.member = static_cast<int>(i);
            one = eachElement(indexSlot, count, std::move(one));
            if (!assigning) one = guardElements(std::move(one), built, indexSlot, pos);
        }
        body.push_back(std::move(one));
        finished.resize(body.size());
        if (!assigning && memberHasDestructor(mt)) {
            finished.back() = Alive{ "this", thisSlot, type };
            finished.back().owner = type;
            finished.back().member = static_cast<int>(i);
        }
    }

    // **`a = b` is an expression and has to have a value**, and the value is the object
    // assigned to. The declared return type is `X &`, and a reference is a pointer
    // everywhere below the parser, so what the function actually returns is `this`.
    const Type *returns = types_.get(Kind::Void);
    if (assigning) {
        returns = types_.pointerTo(type);
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        body.push_back(StmtPtr(new Return(std::move(me))));
    }

    bool guarded = false;
    StmtPtr built = withVirtualBases(std::move(vbInit), vbs,
                                     guardConstruction(std::move(body), std::move(finished), pos, &guarded),
                                     pos);
    current_->functions.push_back(Function(cls + "::" + (assigning ? "operator="
                                                                  : cls),
                                           returns, std::move(params), std::move(built),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    showMemberAs(current_->functions.back(), type, assigning ? std::string("operator=") : localOf(cls));
    current_->functions.back().setHasLandingPads(guarded || own.made());
    current_->functions.back().setInline(true);
    // A constructor is emitted under both of Itanium's names; an operator has
    // one name in either ABI.
    if (!assigning && !target_.microsoftNames()) {
        std::vector<const Type *> ps;
        ps.push_back(srcRef);
        const Type *fnType = types_.functionType(types_.get(Kind::Void), ps, false);
        std::string c2, why;
        if (itaniumConstructorName(cls, type, fnType, false, &c2, &why)) {
            // The same split, with the source forwarded: C1 copies the one
            // virtual base out of `that` and then calls C2 for the rest.
            if (type->hasVirtualBase()) {
                current_->functions.back().setSymbol(c2);
                closeFrame(savedFrame);
                vttSlot_ = savedVtt;
                synthesizeCompleteCtor(type, ps, symbol, c2, true, pos, 0,
                                       moving);
                return;
            }
            current_->functions.back().setAlias(c2);
        }
    }
    closeFrame(savedFrame);
    vttSlot_ = savedVtt;
}

// [class.virtual]'s key function, as the Itanium ABI reads it: the first
// non-pure virtual member declared in the class and not defined inline. Where
// that one is not defined in this unit, the vtable and type_info are not either.
bool Parser::keyFunctionUndefined(const std::string &tag) const {
    const Signature *key = keyFunction(tag);
    return key != nullptr && !key->defined;
}

// Whether the class has a key function and this unit defines it: then the group is emitted here.
bool Parser::hasKeyFunction(const std::string &tag) const {
    const Signature *key = keyFunction(tag);
    return key != nullptr && key->defined;
}

const Parser::Signature *Parser::keyFunction(const std::string &tag) const {
    std::map<std::string, std::vector<VSlot> >::const_iterator vt = vtables_.find(tag);
    for (std::size_t i = 0; i < functions_.size(); i++) {
        const Signature &f = functions_[i];
        // A deleted function is defined where it is declared, so no unit is the one that defines it.
        if (f.owner != tag || !f.isVirtual || f.implicit || f.inlineBody || f.deleted) continue;
        bool pure = false;
        if (vt != vtables_.end())
            for (std::size_t k = 0; k < vt->second.size(); k++)
                if (vt->second[k].pure && overrides(vt->second[k], f.name, f.params, f.constThis, f.refThis, f.volatileThis))
                    pure = true;
        if (pure) continue;
        return &f;
    }
    return nullptr;
}

// A class whose key function lives in another unit gets its vtable, its
// type_info and its deleting destructor from there: clang emits none of the
// three, and so `std::exception`'s are the runtime's rather than a copy.
void Parser::pruneExternalVtables(Program &program) {
    if (target_.microsoftNames()) return;   // cl emits every vftable, as a COMDAT
    for (std::map<std::string, std::vector<std::string> >::const_iterator it =
             classSymbols_.begin(); it != classSymbols_.end(); ++it) {
        const Type *cls = findTypedef(it->first);
        if (cls != nullptr && cls->unqualified()->isSpecialization()) continue;
        if (!keyFunctionUndefined(it->first)) continue;
        std::vector<std::string> gone = it->second;
        gone.push_back(deletingDestructorSymbol(it->first));
        for (std::size_t g = 0; g < gone.size(); g++) {
            for (std::size_t i = program.globals.size(); i-- > 0; )
                if (program.globals[i].symbol == gone[g])
                    program.globals.erase(program.globals.begin() + static_cast<long>(i));
            for (std::size_t i = program.functions.size(); i-- > 0; )
                if (program.functions[i].symbol() == gone[g])
                    program.functions.erase(program.functions.begin() + static_cast<long>(i));
        }
    }
}

// **A candidate instantiated and never chosen has no body in clang's or cl's object**, [temp.inst]/3 -
// a member template's is replayed as it is formed, so what was emitted for one is taken back here.
void Parser::pruneUnchosenCandidates(Program &program) {
    for (std::size_t k = 0; k < candidateSpecializations_.size(); k++) {
        const Signature &f = functions_[candidateSpecializations_[k]];
        if (f.used) continue;
        for (std::size_t i = program.functions.size(); i-- > 0; )
            if (program.functions[i].symbol() == f.symbol)
                program.functions.erase(program.functions.begin() + static_cast<long>(i));
    }
}

namespace {

// Every symbol a body names: a call, and a variable or function used as a value.
class SymbolsNamed final : public Visitor {
public:
    std::vector<std::string> *out;
    explicit SymbolsNamed(std::vector<std::string> *o) : out(o) {}
    void visit(const Num &) override {}
    void visit(const Var &n) override { out->push_back(n.symbol()); }
    void visit(const StrLit &e) override { out->push_back(e.label()); }
    void visit(const Goto &) override {}
    void visit(const Break &) override {}
    void visit(const Continue &) override {}
    void visit(const FuncletLeave &) override {}
    void visit(const Call &n) override {
        out->push_back(n.symbol());
        if (n.callee() != nullptr) n.callee()->accept(*this);
        for (const ExprPtr &a : n.args()) a->accept(*this);
    }
    void visit(const VaStart &n) override { n.list().accept(*this); }
    void visit(const VaArg &n) override { n.list().accept(*this); }
    void visit(const Assign &n) override { n.target().accept(*this); n.value().accept(*this); }
    void visit(const Unary &n) override { n.operand().accept(*this); }
    void visit(const Binary &n) override { n.lhs().accept(*this); n.rhs().accept(*this); }
    void visit(const Postfix &n) override { n.target().accept(*this); }
    void visit(const Cast &n) override { n.value().accept(*this); }
    void visit(const Comma &n) override { n.left().accept(*this); n.right().accept(*this); }
    void visit(const Conditional &n) override {
        n.cond().accept(*this); n.thenArm().accept(*this); n.elseArm().accept(*this);
    }
    void visit(const MemberAccess &n) override { n.object().accept(*this); }
    void visit(const ExprStmt &n) override { n.expr().accept(*this); }
    void visit(const Return &n) override { if (n.hasValue()) n.value().accept(*this); }
    void visit(const Block &n) override { for (const StmtPtr &s : n.body()) s->accept(*this); }
    void visit(const If &n) override {
        n.cond().accept(*this); n.thenArm().accept(*this);
        if (n.elseArm() != nullptr) n.elseArm()->accept(*this);
    }
    void visit(const While &n) override { n.cond().accept(*this); n.body().accept(*this); }
    void visit(const DoWhile &n) override { n.body().accept(*this); n.cond().accept(*this); }
    void visit(const For &n) override {
        if (n.init() != nullptr) n.init()->accept(*this);
        if (n.cond() != nullptr) n.cond()->accept(*this);
        if (n.step() != nullptr) n.step()->accept(*this);
        n.body().accept(*this);
    }
    void visit(const Switch &n) override { n.cond().accept(*this); n.body().accept(*this); }
    void visit(const Case &n) override { n.body().accept(*this); }
    void visit(const Label &n) override { n.body().accept(*this); }
    void visit(const Try &n) override {
        for (const std::string &t : n.types()) out->push_back(t);
        for (const StmtPtr &s : n.body()) s->accept(*this);
        if (n.hasPad()) n.pad().accept(*this);
        if (n.cleanup() != nullptr) n.cleanup()->accept(*this);
        for (const MsHandler &h : n.handlers())
            if (h.body != nullptr) h.body->accept(*this);
    }
};

}

// **An inline function, or a vtable group with no key function, nothing here uses is not emitted** -
// [basic.def.odr]/3, and what clang, g++ and cl do. Reached from every function and global not held;
// a symbol is dropped only if it was held and nothing reachable names it or its alias.
void Parser::pruneUnusedInline(Program &program) {
    for (std::size_t i = 0; i < functions_.size(); i++)
        if (functions_[i].fromTemplate) discardableInline_.erase(functions_[i].symbol);
    // What the compiler writes is inline by [class.copy] and [class.dtor]: reached, or not emitted.
    std::set<std::string> fromTemplate;
    for (std::size_t i = 0; i < functions_.size(); i++)
        if (functions_[i].fromTemplate) fromTemplate.insert(functions_[i].symbol);
    std::set<std::string> held = discardableInline_;
    for (const Function &f : program.functions)
        if (f.isInline() && fromTemplate.count(f.symbol()) == 0) held.insert(f.symbol());
    // [temp.explicit]/8: `template struct Box<double>;` defines every member here, called or not.
    for (const std::string &s : explicitlyInstantiatedSymbols_) held.erase(s);
    // A class whose key function is defined here owns its group in this unit, built or not.
    std::vector<std::string> keyed;
    if (!target_.microsoftNames())
        for (std::map<std::string, std::vector<std::string> >::const_iterator it =
                 classSymbols_.begin(); it != classSymbols_.end(); ++it)
            if (hasKeyFunction(it->first))
                for (const std::string &sym : it->second) keyed.push_back(sym.substr(4));
    auto heldGlobal = [&](const Global &g) {
        if (!g.isInline) return false;
        const std::string &n = g.symbol;
        if (target_.microsoftNames()) return n.compare(0, 4, "??_7") == 0;
        if (n.compare(0, 4, "_ZTV") != 0 && n.compare(0, 4, "_ZTI") != 0 &&
            n.compare(0, 4, "_ZTS") != 0 && n.compare(0, 4, "_ZTT") != 0 &&
            n.compare(0, 4, "_ZTC") != 0)
            return false;
        for (const std::string &k : keyed)         // _ZTC names its layout class first
            if (n.compare(4, k.size(), k) == 0) return false;
        return true;
    };
    if (held.empty()) {
        bool any = false;
        for (const Global &g : program.globals) any = any || heldGlobal(g);
        if (!any) return;
    }
    // A node is a function (its index) or a global (its index past the functions).
    const std::size_t nf = program.functions.size();
    std::map<std::string, std::size_t> bySymbol;
    for (std::size_t i = 0; i < nf; i++) {
        bySymbol[program.functions[i].symbol()] = i;
        if (!program.functions[i].alias().empty()) bySymbol[program.functions[i].alias()] = i;
    }
    for (std::size_t i = 0; i < program.globals.size(); i++)
        bySymbol[program.globals[i].symbol] = nf + i;
    std::vector<bool> reached(nf + program.globals.size(), false);
    std::set<std::string> everyName;
    std::vector<std::string> named;
    named.push_back(program.initFunction);
    for (const MicrosoftThrow &t : program.msThrows) {
        if (!t.thrown) continue;         // a type only caught needs its descriptor alone
        named.push_back(t.destructor);
        for (const MicrosoftThrow::Catchable &c : t.catchables) named.push_back(c.copyCtor);
    }
    std::vector<std::size_t> work;
    for (std::size_t i = 0; i < nf; i++)
        if (held.count(program.functions[i].symbol()) == 0) {
            reached[i] = true;
            work.push_back(i);
        }
    for (std::size_t i = 0; i < program.globals.size(); i++)
        if (!heldGlobal(program.globals[i])) {
            reached[nf + i] = true;
            work.push_back(nf + i);
        }
    for (;;) {
        for (const std::string &s : named) {
            everyName.insert(s);
            const std::map<std::string, std::size_t>::const_iterator f = bySymbol.find(s);
            if (f != bySymbol.end() && !reached[f->second]) {
                reached[f->second] = true;
                work.push_back(f->second);
            }
        }
        named.clear();
        if (work.empty()) break;
        const std::size_t i = work.back();
        work.pop_back();
        if (i < nf) {
            SymbolsNamed collect(&named);
            program.functions[i].body().accept(collect);
            // The types of its `throw(T...)` list: where the list is the unwinder's table and not a try block in
            // the body (the C6000), the table names their type_info and the body does not - and a class the
            // list alone names had its type_info dropped here, so the program did not link (1.68; q200 b33).
            for (const std::string &s : program.functions[i].exceptionSpec()) named.push_back(s);
        } else {
            const Global &g = program.globals[i - nf];
            named.push_back(g.prefixWord);
            for (const GlobalPiece &p : g.init) named.push_back(p.symbol);
        }
    }
    std::vector<Function> kept;
    for (std::size_t i = 0; i < nf; i++)
        if (reached[i]) kept.push_back(std::move(program.functions[i]));
    program.functions.swap(kept);
    std::vector<Global> keptGlobals;
    for (std::size_t i = 0; i < program.globals.size(); i++)
        if (reached[nf + i]) keptGlobals.push_back(std::move(program.globals[i]));
    program.globals.swap(keptGlobals);
    // A literal only a pruned body used goes with it; what is kept names every literal it reads.
    std::vector<StringLit> keptStrings;
    for (StringLit &sl : program.strings)
        if (everyName.count(sl.label) != 0) keptStrings.push_back(std::move(sl));
    program.strings.swap(keptStrings);
    // A Microsoft class keeps its five records where its vftable stayed or something names one.
    std::vector<const Type *> rtti;
    for (const Type *cls : program.rtti) {
        MicrosoftRtti n;
        std::string why;
        if (!microsoftClassRttiNames(cls, &n, &why) || everyName.count(n.locator) ||
            everyName.count(n.descriptor) || everyName.count(n.baseDescriptor) ||
            everyName.count(n.array) || everyName.count(n.hierarchy))
            rtti.push_back(cls);
    }
    program.rtti.swap(rtti);
}

// **To a fixed point, because a body can be what first calls another.** Giving
// Owner its constructor is what calls Held's, and Held's may not have been
// wanted by anything the program wrote.
void Parser::defineImplicitFunctions() {
    for (bool again = true; again; ) {
        again = false;
        for (std::size_t i = 0; i < functions_.size(); i++) {
            if (!(functions_[i].implicit || functions_[i].defaultedOutside) || !functions_[i].used ||
                functions_[i].defined)
                continue;
            functions_[i].defined = true;
            if (!functions_[i].name.empty() && functions_[i].name[0] == '~')
                                                    synthesizeDestructor(i);
            else if (functions_[i].name == "operator=") synthesizeCopy(i, true);
            else if (functions_[i].params.empty() || inheriting_.count(i)) synthesizeDefaultCtor(i);
            else                                    synthesizeCopy(i, false);
            again = true;
        }
    }
}

std::string Parser::staticMemberSymbol(const std::string &cls,
                                       const std::string &name, const Type *t,
                                       Access access, std::size_t pos) {
    if (!target_.microsoftNames()) return itaniumStaticMemberName(cls, findTypedef(cls), name);
    // Microsoft writes the access as a digit where a member function writes a letter,
    // so a static member that changes from private to public changes its symbol on
    // Windows and keeps it on Linux - the same asymmetry, measured the same way.
    const char code = access == Access::Public    ? '2'
                    : access == Access::Protected ? '1'
                                                  : '0';
    std::string out, why;
    if (!microsoftStaticMemberName(cls, findTypedef(cls), name, t, code, &out, &why))
        src_.fail(pos, "'" + cls + "::" + name + "' cannot be given a name the "
                       "linker can hold: " + why);
    return out;
}

// `static int total;` inside a class: one object shared by every object of the class,
// taking no room in any of them, so nothing here touches the layout. What it needs is
// a name the linker can hold and a definition outside the class to go with it.
void Parser::declareStaticMember(const std::string &cls, Type *owner,
                                 const Declared &d, Access access,
                                 bool threadLocal, bool isConstexpr) {
    if (cls.empty())
        src_.fail(d.pos, "a static member needs a class with a name - this one "
                         "is anonymous");
    if (owner->findMember(d.name) != nullptr)
        src_.fail(d.pos, "'" + cls + "::" + d.name + "' is a static member and "
                         "an ordinary one, and it can only be one of them");
    for (const Type::StaticMember &had : owner->staticMembers())
        if (had.name == d.name)
            src_.fail(d.pos, "'" + cls + "::" + d.name + "' is declared twice");

    Type::StaticMember s;
    s.name = d.name;
    s.type = d.type;
    s.access = access;
    s.isThreadLocal = threadLocal;

    // **[class.static.data]/3: a `static constexpr` member of any literal type is initialised here.**
    if (isConstexpr && !d.type->isInteger() && (peek().is("=") || peek().is("{"))) {
        s.symbol = staticMemberSymbol(cls, d.name, d.type, access, d.pos);
        owner->addStaticMember(s);
        defineConstexprStaticMember(cls + "::" + d.name, s.symbol, d);
        return;
    }
    // **`static const int k = 5;` written in the class needs no definition**, measured
    // rather than assumed: cl emits no symbol for one and folds the value in wherever
    // it is read. Anything else with an initialiser here is refused.
    if (consume("=")) {
        if (!d.type->isConst() || d.type->isVolatile() || !d.type->isInteger())
            src_.fail(d.pos, "'" + cls + "::" + d.name + "' is initialised "
                             "inside the class, and only a 'static const' of "
                             "integer type may be - write the value on the "
                             "definition outside the class instead");
        s.folded = true;
        s.value = constantExpression("a static member's value");
    } else if (d.type->isArray() && d.type->length() < 0) {
        src_.fail(d.pos, "'" + cls + "::" + d.name + "' has no length, and a "
                         "static member cannot take one from its definition - "
                         "the class is what says how big it is");
    }

    // A static member is one object for the whole program, so it has a name
    // outside this file whatever else it is.
    s.symbol = staticMemberSymbol(cls, d.name, d.type, access, d.pos);
    owner->addStaticMember(s);
}

// The value folded where the class declares it, and laid down there as a weak definition: a constant
// expression reads the value, and a read at run time the object, defined or not outside the class.
void Parser::defineConstexprStaticMember(const std::string &full, const std::string &symbol, const Declared &d) {
    inClassConstexprStatics_.insert(symbol);
    std::vector<GlobalPiece> pieces;
    // A template's own pattern has nothing to fold; each specialization reads it again.
    if (patternOnly_) {
        for (int depth = 0; !(depth == 0 && (peek().is(";") || peek().is(","))); at_++) {
            if (peek().is("(") || peek().is("{") || peek().is("[")) depth++;
            if (peek().is(")") || peek().is("}") || peek().is("]")) depth--;
        }
        return;
    }
    const Type *cls = d.type->unqualified();
    if (cls->isStructOrUnion() && !cls->tag().empty() && hasConstructors(cls->tag())) {
        Declared m = d;
        m.name = full;
        dynamicInitialise(m, symbol, std::string(), false, &pieces);
    } else {
        consume("=");
        Init in = parseInitialiser();
        ConstObject value;
        StaticConst rec;
        if (d.type->isFloating() && !in.isList && in.value != nullptr && foldFloating(*in.value, &rec.dvalue)) {
            rec.dknown = true;
            staticConsts_[symbol] = rec;
            flattenScalar(d.type, in, 0, pieces);
        } else if ((cls->isStructOrUnion() || (cls->isArray() && (cls->innermostElement()->isArithmetic() ||
                                                                   cls->innermostElement()->isPointer()))) &&
                   constObjectFromInit(d.type, in, 0, &value)) {
            constObjects_[symbol] = value;
            constObjectPieces(value, pieces);
        } else if (cls->isPointer() && staticallyInitialisable(d.type, in) && constantPointerInit(in, symbol)) {
            flattenInit(d.type, in, 0, pieces);
        } else {
            refuseConstexprClassObject(full, d.pos);
        }
    }
    current_->globals.push_back(Global{ full, symbol, d.type, pieces, true, false, true });
    current_->globals.back().isInline = true;
}

// `int Counter::total = 0;` at file scope - the definition the declaration inside the
// class asked for. An ordinary global the class gave its name to, so all this adds is
// finding which member it is and taking the symbol from it.
void Parser::defineStaticMember(Declared &d, Program &program, bool threadLocal) {
    const Type *owner = findTypedef(d.qualifier);
    if (owner == nullptr || !owner->isStructOrUnion())
        src_.fail(d.pos, "'" + d.qualifier + "' is not a class");
    const Type::StaticMember *s = owner->findStaticMember(d.name);
    if (s == nullptr)
        src_.fail(d.pos, "'" + d.qualifier + "' declares no static member '" +
                         d.name + "'");
    // [dcl.stc]/4 wants `thread_local` on the definition as on the declaration; clang's words.
    if (threadLocal != s->isThreadLocal)
        src_.fail(d.pos, std::string(threadLocal ? "thread-local" : "non-thread-local") + " declaration of '" +
                         d.name + "' follows " + (threadLocal ? "non-thread-local" : "thread-local") + " declaration");
    // What the member's definition adds to the unit: what initialises it, and that it is here.
    const ThreadLocalObject tlo(this, threadLocal);
    if (threadLocal) tlsDefining(s->symbol, true);
    const StaticMemberScope scope(this, owner);
    // [class.static.data]/3: `constexpr P S::k;` defines what the class initialised, already laid down.
    if (inClassConstexprStatics_.count(s->symbol)) {
        if (!peek().is(";") && !peek().is(","))
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' was initialised inside the class, so "
                             "its definition outside it takes no initialiser");
        if (!peek().is(",")) expect(";");
        return;
    }
    if (s->type->unqualified() != d.type->unqualified() ||
        s->type->isConst() != d.type->isConst())
        src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' was declared '" +
                         s->type->describe() + "' and this defines it as '" +
                         d.type->describe() + "'");
    for (const Global &g : program.globals)
        if (g.symbol == s->symbol)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is defined "
                             "twice");

    // **A static member of class type is built before main** - [basic.start.init]
    // - in the init function, under a guard where the class is a specialization
    // so a second translation unit's copy does not build it twice.
    if (const Type *cls = memberClass(s->type))
        if (!cls->tag().empty() &&
            hasConstructors(cls->tag())) {
            const std::string full = d.qualifier + "::" + d.name;
            Declared m = d;
            m.name = full;
            m.type = s->type;
            const std::string helper = target_.microsoftNames()
                ? atexitHelperName(s->symbol + "@") : std::string();
            dynamicInitialise(m, s->symbol, helper, owner->isSpecialization());
            if (!peek().is(",")) expect(";");
            // Not isConst whatever the member says: the constructor writes it.
            program.globals.push_back(Global{ full, s->symbol, s->type,
                                              std::vector<GlobalPiece>(), false,
                                              false, false });
            program.globals.back().isInline = owner->isSpecialization();
            program.globals.back().isThreadLocal = threadLocal;
            if (threadLocal) tlsDynamic_.insert(s->symbol);
            return;
        }

    std::vector<GlobalPiece> pieces;
    bool hasInit = false, stored = false;
    // **A static reference member is a slot holding an address**, bound as one at
    // namespace scope is: a global's address in the image, anything else before main.
    if (s->type->isReference()) {
        Declared m = d;
        m.name = d.qualifier + "::" + d.name;
        m.type = s->type;
        const FunctionState outer = enterInitFunction();
        bindStaticReference(m, s->symbol, pieces, hasInit, &initSink());
        leaveInitFunction(outer);
        if (!peek().is(",")) expect(";");
        program.globals.push_back(Global{ m.name, s->symbol,
                                          types_.pointerTo(s->type->referent()),
                                          std::move(pieces), hasInit, false, false });
        program.globals.back().isInline = owner->isSpecialization();
        program.globals.back().isThreadLocal = threadLocal;
        if (threadLocal && !hasInit) tlsDynamic_.insert(s->symbol);
        return;
    }
    if (consume("=") || atBracedInitialiser(d.name)) {
        Init in = parseInitialiser();
        // Read while the initialiser tree is still in scope, as the
        // namespace-scope path does: flattenInit answers in bytes rather than
        // in the value the read-back wants.
        if (s->type->isConst()) {
            StaticConst rec;
            long long iv = 0;
            if (constantInitialiser(s->type, in, &iv)) {
                rec.known = true; rec.value = iv;
            }
            if (s->type->isFloating() && !in.isList && in.value != nullptr) {
                bool p53 = false, x87 = false;
                long double dv = 0;
                if (foldDouble(*in.value, target_, &dv, &p53, &x87)) {
                    rec.dknown = true; rec.dvalue = dv;
                }
            }
            if (rec.known || rec.dknown) staticConsts_[s->symbol] = rec;
        }
        // `T Tm<T>::st = T();` with T a trivial class, or an initialiser that
        // does not fold: stored before main, as the namespace-scope path does
        // (A13). A template's member is stored under its once-guard.
        if (staticallyInitialisable(s->type, in)) {
            flattenInit(s->type, in, 0, pieces);
            hasInit = true;
        } else {
            dynamicInitialiseStaticMember(d.qualifier + "::" + d.name, s->symbol,
                                          s->type, in, owner->isSpecialization());
            stored = true;
        }
    } else if (s->folded) {
        // `const int Outer::K;` defines what the class initialised, [class.static.data]/3, so the
        // object holds the declaration's value - `&Outer::K` read a zero (1.27, 2026-10-21, review
        // of 1.25, item 3).
        Init in;
        in.pos = d.pos;
        in.value.reset(new Num(s->value));
        in.value->setType(s->type->unqualified());
        flattenInit(s->type, in, 0, pieces);
        hasInit = true;
    }
    if (!peek().is(",")) expect(";");

    program.globals.push_back(Global{ d.qualifier + "::" + d.name, s->symbol,
                                      s->type, std::move(pieces), hasInit, false,
                                      s->type->isConst() && !stored });
    // A template's static member is defined by every unit that uses it, and
    // the linker keeps one - a weak object, as its vtable is.
    program.globals.back().isInline = owner->isSpecialization();
    program.globals.back().isThreadLocal = threadLocal;
    if (threadLocal && stored) tlsDynamic_.insert(s->symbol);
}

// [expr.prim.general]/13: nothing reads it, so an lvalue of the member's type at no
// address answers every question sizeof and decltype may ask - `*(T *)0`, never emitted.
ExprPtr Parser::memberWithoutObject(const Type *cls, const Member &m, std::size_t pos) {
    checkAccessible(cls, m, pos);
    if (m.width != 0)
        src_.fail(pos, "sizeof cannot be applied to '" + m.name + "', which is a bit-field");
    const Type *t = m.type->isReference() ? m.type->referent() : m.type;
    ExprPtr zero(new Num(0LL));
    zero->setType(types_.intType());
    ExprPtr ptr(new Cast(types_.pointerTo(t), std::move(zero)));
    ptr->setType(types_.pointerTo(t));
    ExprPtr deref(new Unary('*', std::move(ptr)));
    deref->setType(t);
    return deref;
}

// `S::x`, `Outer::Inner::y`, `n::S::q`: the longest prefix naming a class that has
// the data member wins, as the static member walk in primary() takes it.
ExprPtr Parser::unevaluatedMember() {
    std::string q = peek().text;
    const Type *owner = nullptr;
    const Member *found = nullptr;
    std::size_t consumed = 0;
    for (std::size_t k = 1; peekAt(k).is("::") && peekAt(k + 1).kind == TokenKind::Ident;
         k += 2) {
        if (const Type *cls = findTypedef(q))
            if (cls->isStructOrUnion())
                if (const Member *m = cls->findMember(peekAt(k + 1).text)) {
                    owner = cls;
                    found = m;
                    consumed = k + 2;
                }
        q += "::" + peekAt(k + 1).text;
    }
    if (found == nullptr || peekAt(consumed).is("(")) return nullptr;
    const std::size_t pos = peekAt(consumed - 1).pos;
    at_ += consumed;
    return memberWithoutObject(owner, *found, pos);
}

// Naming a static member, however it was reached. A folded one is its value
// and has no storage at all; every other is the one global the class named.
ExprPtr Parser::staticMemberRef(const Type *owner, const Type::StaticMember &s,
                                const std::string &cls, std::size_t pos) {
    if (s.access != Access::Public && !insideAccessOf(owner, s.access) &&
        !isFriendOf(owner))
        src_.fail(pos, "'" + cls + "::" + s.name + "' is " +
                       (s.access == Access::Private ? "private" : "protected"));
    if (s.folded) {
        ExprPtr n(new Num(s.value));
        n->setType(s.type);
        return n;
    }
    if (unevaluated_ == 0) usedStaticMembers_.insert(s.symbol);
    Var *v = Var::global(cls + "::" + s.name);
    v->setSymbol(s.symbol);
    // Through the wrapper unless this unit has already defined it with a constant initialiser - a
    // definition still to come, in this unit or another, may build it dynamically.
    if (s.isThreadLocal) tlsVar(v, tlsDynamic_.count(s.symbol) != 0 || tlsDefined_.count(s.symbol) == 0, true);
    ExprPtr n(v);
    n->setType(s.type);
    return useReference(std::move(n));      // a reference member is read through its slot
}

// A member function declaration, keyed under "Class::name" in the one table
// every function lives in.
void Parser::checkNotAbstract(const Type *t, std::size_t pos,
                              const std::string &what) {
    const Type *c = t;
    while (c != nullptr && c->isArray()) c = c->pointee();
    if (c == nullptr || !c->isStructOrUnion() || !c->abstract()) return;
    std::string why = what + " is '" + c->describe() + "', which is abstract - ";
    const std::vector<VSlot> &slots = vtables_[c->tag()];
    for (std::size_t i = 0; i < slots.size(); i++)
        if (slots[i].pure) {
            why += "'" + slots[i].name + "' is pure and nothing has overridden "
                   "it, so an object of this class would have a slot with no "
                   "function in it";
            src_.fail(pos, why);
        }
    src_.fail(pos, why + "it has a pure virtual nothing has overridden");
}

void Parser::declareMember(const std::string &cls, const Declared &d,
                           bool constThis, RefQual refThis, Access access, bool inUnion,
                           bool isVirtual, bool isStatic, bool isPure,
                           VirtSpecifiers virt) {
    // [class.union]/2: a union has member functions, and none of them is virtual.
    if (inUnion && isVirtual)
        src_.fail(d.pos, "unions cannot have virtual functions - [class.union]/2");

    const Type *fn = d.type;
    const bool volatileThis = pendingVolatileThis_;
    pendingVolatileThis_ = false;
    checkOperatorDeclarable(d.name, fn->params(), true, d.pos, false, fn->returns());
    // `explicit operator bool()`: the keyword was read in the class body and reached no Signature,
    // so `bool b = f` took the function as [class.conv.fct]/2 forbids (1.28, 2026-10-22, review of
    // 1.25, neg07). Taken here, where the member's Signature is made.
    const bool explicitConversion = pendingExplicitConversion_ && isConversionName(d.name);
    pendingExplicitConversion_ = false;

    std::string key = cls + "::" + d.name;
    std::vector<std::size_t> &set = functionIndex_[key];

    const std::vector<const Type *> &params = fn->params();
    for (std::size_t k = 0; k < set.size(); k++) {
        const Signature &f = functions_[set[k]];
        if (!sameParameters(f.params, params)) continue;
        if (f.constThis == constThis && f.volatileThis == volatileThis && f.refThis == refThis)
            src_.fail(d.pos, "'" + key + "' is declared twice");
        if (mixesRefQualifier(f.refThis, refThis))
            src_.fail(d.pos, refQualifierMixRefusal(refThis, f.refThis));
    }

    if (inUnion && isVirtual)
        src_.fail(d.pos, "a union cannot have a virtual function");

    // **A static member takes no slot and overrides nothing.**
    if (isStatic) {
        const std::string sym = memberSymbol(cls, d.name, fn, access, false,
                                             RefQual::None, d.pos, false, true);
        requireDefaultsAreASuffix(pendingDefaults_, pendingDefaultsAt_);
        if (!pendingDefaults_.empty()) {
            defaultArgs_[sym] = pendingDefaults_;
            defaultArgNamespace_[sym] = namespaceStack_;
            // A class template's, read with its arguments as a non-static member's are (1.82: `n = P - 1` of a
            // static member, called from outside, found no P).
            if (!instantiatingParams_.empty())
                defaultArgBindings_[sym] = DefaultBinding{ instantiatingParams_, instantiatingBinding_, instantiatingValues_, Packs() };
        }
        pendingDefaults_.clear();
        set.push_back(functions_.size());
        functions_.push_back(Signature{
            d.name, sym,
            fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
            cls, false, access, false });
        functions_.back().isStaticMember = true;
        functions_.back().isNoexcept = pendingNoexcept_;
        keepDeferredSpec();
        functions_.back().spec = pendingSpec_;
        pendingNoexcept_ = false;
        return;
    }

    // **A slot is taken once and then kept**: an override replaces the entry the base
    // put there, matching on the signature minus the return type. **And finding that
    // slot is what makes this virtual**, keyword or not - so the search runs first.
    std::vector<VSlot> &slots = vtables_[cls];
    std::size_t slot = slots.size();
    bool foundBase = false;
    for (std::size_t i = 0; i < slots.size(); i++) {
        // clang refuses an override that drops or adds the ref-qualifier as [over.load]/2 does.
        if (slots[i].name == d.name && sameParameters(slots[i].params, params) &&
            mixesRefQualifier(slots[i].refThis, refThis))
            src_.fail(d.pos, refQualifierMixRefusal(refThis, slots[i].refThis));
        if (!overrides(slots[i], d.name, params, constThis, refThis, volatileThis)) continue;
        checkOverrideReturn(slots[i], fn->returns(), cls, d.name, d.pos);
        checkNotFinal(slots[i], cls, d.name, d.pos);
        slot = i;
        isVirtual = foundBase = true;
        break;
    }

    // **On Microsoft such an override takes no slot here**: it is entered at the first table holding
    // the slot it overrides, `this` pointing at that vfptr (Phase 6, cl's "this adjustor").
    int msAdjust = 0;
    bool msElsewhere = false;
    const Type *msVbase = nullptr;
    if (!foundBase && target_.microsoftNames())
        if (const Type *self = findTypedef(cls)) {
            const std::vector<Type::BaseSpec> &bs = self->bases();
            for (std::size_t bi = 0; bi < bs.size() && !foundBase; bi++) {
                if (!bs[bi].type->polymorphic()) continue;
                const std::vector<MsVftable> theirs = msVftables(bs[bi].type);
                for (std::size_t t = 0; t < theirs.size() && !foundBase; t++) {
                    if (theirs[t].vbase != nullptr) continue;   // met as this class's own virtual base
                    for (std::size_t i = 0; i < theirs[t].slots.size(); i++)
                        if (overrides(theirs[t].slots[i], d.name, params, constThis, refThis)) {
                            checkOverrideReturn(theirs[t].slots[i], fn->returns(), cls, d.name, d.pos);
                            checkNotFinal(theirs[t].slots[i], cls, d.name, d.pos);
                            isVirtual = foundBase = msElsewhere = true;
                            if (bs[bi].isVirtual) msVbase = bs[bi].type->unqualified();
                            msAdjust = (bs[bi].isVirtual ? 0 : bs[bi].offset) + theirs[t].at;
                            break;
                        }
                }
            }
        }
    // **The slots that came down are the primary base's**, and a class may
    // override a virtual of any base - a second one, or a virtual one, whose
    // function then takes a new slot here and a thunk in that base's table.
    if (!isVirtual)
        if (const Type *self = findTypedef(cls)) {
            const std::vector<Type::BaseSpec> &bs = self->bases();
            for (std::size_t bi = 0; bi < bs.size() && !isVirtual; bi++) {
                std::map<std::string, std::vector<VSlot> >::const_iterator it =
                    vtables_.find(bs[bi].type->tag());
                if (it == vtables_.end()) continue;
                for (std::size_t i = 0; i < it->second.size(); i++)
                    if (overrides(it->second[i], d.name, params, constThis, refThis, volatileThis)) {
                        checkOverrideReturn(it->second[i], fn->returns(), cls, d.name, d.pos);
                        checkNotFinal(it->second[i], cls, d.name, d.pos);
                        isVirtual = foundBase = true;
                        break;
                    }
            }
        }
    // [class.virtual]/4: `override` wants a base's virtual to replace, and `final` a virtual to mark.
    if (virt.isOverride && !foundBase)
        src_.fail(virt.overrideAt, "'" + cls + "::" + d.name + "' is marked 'override' but "
                                   "overrides nothing - no base declares a virtual '" + d.name +
                                   "' with these parameters");
    if (virt.isFinal && !isVirtual)
        src_.fail(virt.finalAt, "'" + cls + "::" + d.name + "' is marked 'final' and is not "
                                "virtual - only a virtual function can be");
    // Asked here and not where `= 0` is read, because a base's slot makes a function virtual without the keyword.
    if (isPure && !isVirtual)
        src_.fail(d.pos, "'= 0' makes a function pure, and only a virtual one can be - '" +
                         d.name + "' is not declared 'virtual' and overrides nothing");

    const unsigned cvThis = (constThis ? Type::kThisConst : 0u) | (volatileThis ? Type::kThisVolatile : 0u);
    const std::string symbol = memberSymbol(cls, d.name, fn, access, cvThis,
                                            refThis, d.pos, isVirtual);
    requireDefaultsAreASuffix(pendingDefaults_, pendingDefaultsAt_);
    if (!pendingDefaults_.empty()) defaultArgs_[symbol] = pendingDefaults_;
    if (!pendingDefaults_.empty() && !instantiatingParams_.empty())   // a class template's, read with its arguments (R17)
        defaultArgBindings_[symbol] = DefaultBinding{ instantiatingParams_, instantiatingBinding_, instantiatingValues_, Packs() };
    if (!pendingDefaults_.empty())
        defaultArgNamespace_[symbol] = namespaceStack_;
    pendingDefaults_.clear();
    set.push_back(functions_.size());
    functions_.push_back(Signature{
        d.name, symbol,
        fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
        cls, constThis, access, isVirtual });
    functions_.back().isNoexcept = pendingNoexcept_;
    keepDeferredSpec();
    functions_.back().spec = pendingSpec_;
    functions_.back().refThis = refThis;
    functions_.back().volatileThis = volatileThis;
    functions_.back().isExplicit = explicitConversion;
    pendingNoexcept_ = false;

    if (!isVirtual) return;
    if (target_.microsoftNames()) {
        msVirtualDecls_[symbol] = MsVirtualDecl{ cls, d.name, fn, constThis, refThis };
        if (isPure) msPure_.insert(symbol);
        if (msElsewhere) {
            if (msVbase != nullptr) msVbaseAdjust_[symbol] = MsVbaseAdjust{ cls, msVbase, msAdjust };
            else if (msAdjust != 0) msThisAdjust_[symbol] = msAdjust;
            return;
        }
    }

    // **A pure virtual's slot holds the runtime's trap, not this function.**
    const std::string entry = isPure ? pureVirtualSymbol() : symbol;
    if (slot < slots.size()) {
        slots[slot].symbol = entry;
        slots[slot].pure = isPure;
        slots[slot].returns = fn->returns();
        slots[slot].isFinal = virt.isFinal;
        return;
    }
    // **cl groups a class's own overloads of one name at the first one's
    // slot, latest first** - `g(int) g(double) h() g(char)` lays `g(char)
    // g(double) g(int) h()`, measured 2026-09-17 - where Itanium appends.
    std::size_t at = slots.size();
    if (target_.microsoftNames()) {
        std::map<std::string, std::size_t>::const_iterator own = ownSlotsFrom_.find(cls);
        for (std::size_t i = own == ownSlotsFrom_.end() ? 0 : own->second; i < slots.size(); i++)
            if (slots[i].name == d.name) { at = i; break; }
    }
    slots.insert(slots.begin() + static_cast<std::ptrdiff_t>(at),
                 VSlot{ d.name, entry, params, constThis, isPure, fn->returns(), virt.isFinal,
                        refThis, volatileThis });
}

void Parser::checkNotFinal(const VSlot &s, const std::string &cls, const std::string &name,
                           std::size_t pos) const {
    if (!s.isFinal) return;
    src_.fail(pos, "'" + cls + "::" + name + "' overrides a virtual function marked "
                   "'final' - [class.virtual]/4 - and nothing may override one");
}

// A member function's linkage name. Never plain, and never affected by
// `extern "C"`: a member cannot have C linkage, so the two ABIs are the only
// choice here.
std::string Parser::memberSymbol(const std::string &cls, const std::string &name,
                                 const Type *fn, Access access, unsigned cvThis,
                                 RefQual refThis, std::size_t pos, bool isVirtual,
                                 bool isStatic) {
    // Q public, I protected, A private - the Microsoft ABI puts the access in
    // the name and Itanium does not, both measured.
    const char code = isStatic && access == Access::Public    ? 'S'
                    : isStatic && access == Access::Protected ? 'K'
                    : isStatic                                ? 'C'
                    : isVirtual        ? 'U'
                    : access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    // A class defined inside a function body.
    const std::string *owner = localOwnerOf(cls);

    std::string out, why;
    const Type *shown = target_.microsoftNames() ? manglingType(fn) : fn;
    bool ok = target_.microsoftNames()
            ? (owner != nullptr
                   ? microsoftLocalMemberName(*owner, cls, findTypedef(cls), name,
                                              shown, code, cvThis, refThis, &out, &why)
                   : microsoftMemberName(cls, findTypedef(cls), name, shown, code,
                                         cvThis, refThis, &out, &why))
            : (owner != nullptr
                   ? itaniumLocalMemberName(*owner, cls, findTypedef(cls), name,
                                            fn, cvThis, refThis, &out, &why)
                   : itaniumMemberName(cls, findTypedef(cls), name, fn, cvThis,
                                       refThis, &out, &why));
    if (!ok)
        src_.fail(pos, "'" + cls + "::" + name + "' cannot be given a name the "
                       "linker can hold: " + why);
    return out;
}

// A variable at namespace scope is mangled by the Microsoft ABI and left
// alone by Itanium. A static one is nobody else's business either way, so it
// keeps the name it was written with.
std::string Parser::dataSymbol(const std::string &name, const Type *type,
                               bool isStatic, std::size_t pos) {
    // [dcl.link]/4: extern "C" names a variable with external linkage only; an internal one
    // is spelled as any internal C++ name is - `_ZL2sv`, or as written on Microsoft.
    if (cLinkage_ > 0 && !isStatic) return name;
    if (!target_.microsoftNames()) return itaniumDataName(name, isStatic);
    // Microsoft mangles a variable only where something outside could name it.
    // An internal one keeps what it was written with - measured against clang,
    // which spells it the same way.
    if (isStatic && name.find("::") == std::string::npos) return name;
    std::string out, why;
    if (!microsoftDataName(name, type, &out, &why))
        src_.fail(pos, "'" + name + "' cannot be given a name the linker can "
                       "hold: " + why);
    return out;
}

// **The parameter list is what identifies a function now, not the name.** A difference
// in the parameters declares a *second* function and only an identical list is a
// redeclaration. The return type is deliberately not part of that search.
void Parser::declareFunction(const std::string &name, const Type *returns,
                             const std::vector<const Type *> &params,
                             bool variadic, bool defining, std::size_t pos,
                             bool internal) {
    // While a specialization is being replayed the function it declares is that
    // specialization, keyed as "twice<int>" from when the call asked for it. **And the
    // namespace goes on here, once**, every table below being keyed by the whole name.
    const std::string plain = instantiationName(name);
    const std::string qualified =
        (cLinkage_ > 0 || plain == "main" || namespaceStack_.empty() ||
         (!instantiationKey_.empty() && plain == instantiationKey_))
            ? plain : namespacePrefix() + plain;
    const std::string &key = qualified;
    checkOperatorDeclarable(key, params, false, pos, internal, returns);
    const bool cName = cLinkage_ > 0 || key == "main" || (microsoftEntry(key) && !internal);
    std::vector<std::size_t> &set = functionIndex_[key];
    const SpecialDef special = takeSpecialDef();
    if (special == SpecialDef::Defaulted)
        src_.fail(pendingSpecialAt_, "'" + plain + "' is not a special member function, and only one of "
                                     "those can be defaulted - [dcl.fct.def.default]/1");
    // [basic.scope.declarative]/4: a function does not take the name of a variable in the same scope.
    if (findGlobalToUpdate(key) != nullptr && instantiationKey_.empty())
        src_.fail(pos, "'" + plain + "' redeclares a variable as a function - [basic.scope.declarative]/4");

    // An inline namespace's member sits under the enclosing name too, and only a qualified
    // declarator reaches it there - `int lib::f(int) {}` defines lib::v2::f - after lib's own.
    for (std::size_t pass = 0, k = 0; pass < (definingQualified_ ? 2u : 1u); k++) {
        if (k == set.size()) { pass++; k = static_cast<std::size_t>(-1); continue; }
        Signature &f = functions_[set[k]];
        // A specialization sits in this list under the template's plain name
        // so that resolution can see it. It is not a declaration of that
        // name, so a function written with the same parameters is a new one.
        if (f.fromTemplate && instantiationKey_.empty()) continue;
        if ((!f.fromTemplate && f.name != key) != (pass == 1)) continue;
        // [class.copy]/2: a member template's specialization is never the copy or move constructor it looks
        // like - `template <class Y> P(const P<Y> &)` at Y = the class itself beside `P(const P &)` (item 30).
        if (memberTemplateInst_ && !f.fromTemplate && !f.owner.empty()) continue;
        if (!memberTemplateInst_ && f.fromMemberTemplate) continue;
        if (f.variadic != variadic || !sameParameters(f.params, params)) continue;
        // [dcl.fct.def.delete]/4: a deleted definition is the first declaration.
        if (special == SpecialDef::Deleted)
            src_.fail(pos, "'" + key + "' is deleted here and was declared before - a deleted "
                           "function has to be deleted on its first declaration");
        if (f.deleted)
            src_.fail(pos, "'" + key + "' was declared '= delete' and may not be declared again");
        // [dcl.link]/5: a redeclaration may leave the linkage out, but not change it.
        if (cName && !f.cLinkage && !internal)
            src_.fail(pos, "'" + key + "' was declared with C++ linkage and this gives it C "
                           "linkage - [dcl.link]/5, every declaration of a function has the same");

        if (f.returns != returns)
            src_.fail(pos, "'" + key + "' was declared to return '" +
                           f.returns->describe() + "' and this says '" +
                           returns->describe() + "' - two functions cannot "
                           "differ in the return type alone");
        // **A declaration and its definition must agree about `noexcept`.** Measured
        // both ways: clang refuses a definition that drops it and one that adds it.
        // The specification is not part of the type, so this is what holds them together.
        if (f.isNoexcept != pendingNoexcept_)
            src_.fail(pos, "'" + key + "' was declared " +
                           (f.isNoexcept ? "'noexcept'" : "without 'noexcept'") +
                           " and this says " +
                           (pendingNoexcept_ ? "'noexcept'" : "nothing") +
                           " - a declaration and its definition have to make "
                           "the same promise");
        // [except.spec]/3 asks the same of `throw(T...)`: the same set of types, or none on both.
        if (f.spec.written != pendingSpec_.written || !f.spec.sameSet(pendingSpec_))
            src_.fail(pos, "'" + key + "' " + (f.spec.written ? "was declared with a 'throw(...)' list"
                                                               : "was declared without a 'throw(...)' list") +
                           " and this " + (pendingSpec_.written ? (f.spec.written ? "names other types" : "has one")
                                                                : "has none") +
                           " - [except.spec]/3, every declaration of a function has the same specification");
        if (defining) {
            if (f.defined) src_.fail(pos, "'" + key + "' is defined twice");
            f.defined = true;
        }
        if (!friendDeclaration_) f.hiddenFriend = false;
        pendingNoexcept_ = false;
        pendingSpec_ = DynamicSpec();

        // **[dcl.fct.default]/4: a later declaration adds defaults, it does not discard them.**
        if (!pendingDefaults_.empty()) {
            // The scope this declaration was written in, for
            // [dcl.fct.default]/5.
            defaultArgNamespace_[f.symbol] = definitionNamespace_.empty() ? namespaceStack_ : definitionNamespace_;
            std::vector<std::size_t> &have = defaultArgs_[f.symbol];
            if (have.size() < pendingDefaults_.size())
                have.resize(pendingDefaults_.size(), 0);
            for (std::size_t i = 0; i < pendingDefaults_.size(); i++) {
                if (pendingDefaults_[i] == 0) continue;
                // **The same default read twice is not a second one.**
                if (have[i] == pendingDefaults_[i]) continue;
                if (have[i] != 0)
                    src_.fail(pos, "'" + key + "' already has a default for "
                                   "parameter " + std::to_string(i + 1) +
                                   " from an earlier declaration, and a later "
                                   "one may add a default where there was none "
                                   "but may not give a second");
                have[i] = pendingDefaults_[i];
            }
            requireDefaultsAreASuffix(have, pos);
        }
        pendingDefaults_.clear();
        cFunctionInNamespace(plain, set[k]);
        return;
    }

    // A new parameter list is a new function, unless the name holds one:
    // refused here, not at the link. [dcl.link]/6 limits the C one, not the
    // set: C++ overloads sit beside C's `abs(int)`, each mangled.
    if (!set.empty()) {
        bool cBefore = false;
        for (std::size_t k = 0; k < set.size(); k++)
            if (functions_[set[k]].cLinkage) cBefore = true;
        if (key == "main" || (cName && cBefore))
            src_.fail(pos, "'" + key + "' cannot be overloaded - " +
                           (key == "main" ? std::string("'main' is one function")
                                           : std::string("[dcl.link]/6, at most one "
                                             "function of a name has C linkage")));
    }

    set.push_back(functions_.size());
    functions_.push_back(Signature{ key,
                                    functionSymbol(key, returns, params, variadic,
                                                   internal, pos),
                                    returns, params, variadic, defining, pos,
                                    cName, std::string(), false,
                                    Access::Public });
    functions_.back().isNoexcept = pendingNoexcept_;
    keepDeferredSpec();
    functions_.back().spec = pendingSpec_;
    functions_.back().deleted = special == SpecialDef::Deleted;
    pendingNoexcept_ = false;
    if (!cName) aliasFunctionKey(key, functions_.size() - 1);
    cFunctionInNamespace(plain, functions_.size() - 1);
    if (pendingExplicitConversion_ && isConversionName(instantiationName(name)))
        functions_.back().isExplicit = true;
    pendingExplicitConversion_ = false;
    requireDefaultsAreASuffix(pendingDefaults_, pendingDefaultsAt_);
    if (!pendingDefaults_.empty()) {
        defaultArgs_[functions_.back().symbol] = pendingDefaults_;
        defaultArgNamespace_[functions_.back().symbol] = definitionNamespace_.empty() ? namespaceStack_ : definitionNamespace_;
        if (memberTemplateInst_ && !instantiatingParams_.empty())
            defaultArgBindings_[functions_.back().symbol] = DefaultBinding{ instantiatingParams_, instantiatingBinding_,
                                                                            instantiatingValues_, Packs() };
    }
    pendingDefaults_.clear();
}

// [dcl.link]/6: a C function declared in a namespace is one entity with every C declaration of its name,
// kept under the plain name, and reached as `ns::cf` as well.
void Parser::cFunctionInNamespace(const std::string &plain, std::size_t index) {
    if (cLinkage_ == 0 || namespaceStack_.empty()) return;
    std::vector<std::size_t> &here = functionIndex_[namespacePrefix() + plain];
    if (std::find(here.begin(), here.end(), index) == here.end()) here.push_back(index);
}

const std::vector<std::size_t> *
Parser::overloadsOf(const std::string &name) const {
    auto it = functionIndex_.find(name);
    if (it == functionIndex_.end() || it->second.empty()) return nullptr;
    return &it->second;
}

// The sole function of that name, or nothing when the name is overloaded. Every caller
// wants one function without having any arguments to choose by, so "there are several"
// is not an answer it can use - each one says so in its own words instead.
const Parser::Signature *Parser::findFunction(const std::string &name) const {
    const std::vector<std::size_t> *set = overloadsOf(name);
    if (set == nullptr || set->size() != 1) return nullptr;
    // Its one caller names the function as a value, which is a use: [dcl.fct.def.delete]/2.
    refuseDeleted(functions_[(*set)[0]], peek().pos);
    return &functions_[(*set)[0]];
}

// The one function of this name with these parameters - the only question a definition
// can ask, since a definition IS a parameter list. Going through lookupFunction broke
// the moment a name could hold two: it answers "which one", and a definition knows.
const Parser::Signature &
Parser::lookupSignature(const std::string &name,
                        const std::vector<const Type *> &params,
                        bool variadic, std::size_t pos) const {
    // The same qualification the declaration used, or a definition written
    // inside a namespace cannot find the prototype it is defining.
    const std::string key = name.find("::") != std::string::npos
                          ? name
                          : qualifyForLookup(instantiationName(name),
                                             &Parser::hasFunctionNamed);
    // The namespace's own function before one an inline namespace put under its name.
    if (const std::vector<std::size_t> *set = overloadsOf(key)) {
        for (int pass = 0; pass < 2; pass++)
            for (std::size_t k = 0; k < set->size(); k++) {
                const Signature &f = functions_[(*set)[k]];
                if ((!f.fromTemplate && f.name != key) == (pass == 1) && f.variadic == variadic &&
                    sameParameters(f.params, params))
                    return f;
            }
    }
    src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");
}

const Parser::Signature &Parser::lookupFunction(const std::string &name,
                                                std::size_t pos) const {
    if (const Signature *s = findFunction(name)) return *s;
    src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");
}

void Parser::blockFunctionDeclaration(const Declared &d) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    declareFunction(d.name, d.type, params, variadic, false, d.pos);
}

// **`Ts... rest` - one thing written, several parameters made.** In a pattern the pack
// stands for itself, which Itanium spells `DpT0_`; in an instantiation it is as many
// parameters as members, named `rest$0` and `rest$1`, which `rest...` expands to.
bool Parser::packParameter(std::vector<const Type *> *types,
                           std::vector<std::string> *names) {
    // `[const] Ts [& | &&]... rest` - the pack read through packDeclarator, which says how each member is wrapped.
    int wrap = 0;
    std::size_t nameAt = 0;
    const std::size_t n = packDeclarator(&wrap, &nameAt);
    if (n == 0) return false;
    auto pk = packs_.find(peekAt(nameAt).text);
    if (pk == packs_.end()) return false;

    std::vector<const Type *> members = pk->second.types;
    const bool pattern = members.size() == 1 &&
                         members[0]->kind() == Kind::TemplateParam;
    at_ += n;
    for (std::size_t i = 0; i < members.size(); i++) members[i] = wrapPackMember(members[i], wrap);
    std::string base;
    if (peek().kind == TokenKind::Ident) { base = peek().text; at_++; }

    if (pattern) {
        types->push_back(types_.packExpansion(members[0]));
        if (names != nullptr) names->push_back(base);
        return true;
    }
    std::vector<std::string> made;
    for (std::size_t i = 0; i < members.size(); i++) {
        types->push_back(types_.withoutConst(members[i]));
        made.push_back(base + "$" + std::to_string(i));
        if (names != nullptr) names->push_back(made.back());
    }
    // Recorded under the *written* name as well, so `rest...` at a call and
    // `sizeof...(rest)` both find it beside `sizeof...(Ts)`.
    if (!base.empty()) {
        PackBinding pb;
        pb.types = members;
        pb.names = made;
        pb.functionParameter = true;
        packs_[base] = pb;
    }
    pk->second.names = made;
    return true;
}

// To the ',' or ')' that ends a default argument, counting brackets so that a call or
// a subscript written inside one keeps its own commas - and a template argument list, whose
// `<` follows a name that is a template ([temp.names]/3): `n = basic_string<C, Tr, A>::npos`.
void Parser::skipDefaultArgument() {
    int depth = 0;
    std::vector<int> angles;   // the bracket depth each open template argument list began at
    for (;;) {
        const Token &t = peek();
        if (t.kind == TokenKind::End)
            src_.fail(t.pos, "this default argument never ends");
        if (t.is("<") && at_ > 0 && tokens_[at_ - 1].kind == TokenKind::Ident &&
            isTemplateName(tokens_[at_ - 1].text, true)) {
            angles.push_back(depth);
        } else if (!angles.empty() && angles.back() == depth && (t.is(">") || t.is(">>"))) {
            angles.pop_back();
            if (t.is(">>") && !angles.empty() && angles.back() == depth) angles.pop_back();
        } else if (t.is("(") || t.is("[") || t.is("{")) {
            depth++;
        } else if (t.is(")") || t.is("]") || t.is("}")) {
            if (depth == 0) return;         // the ')' closing the parameters
            depth--;
        } else if (t.is(",") && depth == 0 && angles.empty()) {
            return;
        }
        at_++;
    }
}

// To the ',' or ';' that ends a member's own initialiser, counting brackets so
// that a call or a braced list written inside one keeps its own commas.
void Parser::skipMemberInitialiser() {
    int depth = 0;
    for (;;) {
        const Token &t = peek();
        if (t.kind == TokenKind::End)
            src_.fail(t.pos, "this member initialiser never ends");
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) {
            if (depth == 0) return;
            depth--;
        } else if (depth == 0 && (t.is(",") || t.is(";"))) return;
        at_++;
    }
}

// **[class.base.init]/9: a member the constructor did not name is initialised
// by the initialiser the class gave it.**
void Parser::refuseDeletedDefaultInit(const Type *t, const std::string &name,
                                      std::size_t pos) {
    const Type *plain = t->unqualified();
    while (plain->isArray()) plain = plain->pointee()->unqualified();
    if (!plain->isStructOrUnion() || plain->tag().empty()) return;
    if (!plain->bases().empty()) return;
    if (hasConstructors(plain->tag())) return;

    const std::vector<Member> &all = plain->members();
    for (std::size_t i = 0; i < all.size(); i++) {
        const bool needsOne = all[i].type->isConst() || all[i].type->isReference();
        if (!needsOne) continue;
        if (memberInit_.find(plain->tag() + "::" + all[i].name) != memberInit_.end())
            continue;
        // A const member of an anonymous union is set where another member of it has an initialiser.
        bool unionSet = false;
        for (std::size_t k = 0; k < all.size() && all[i].anonUnion != nullptr; k++)
            if (all[k].anonUnion == all[i].anonUnion &&
                memberInit_.find(plain->tag() + "::" + all[k].name) != memberInit_.end())
                unionSet = true;
        if (unionSet) continue;
        src_.fail(pos, "'" + name + "' has type '" + plain->describe() +
                       "', whose member '" + all[i].name + "' is " +
                       (all[i].type->isReference() ? "a reference"
                                                   : "const") +
                       " and has no initialiser - [class.ctor] deletes the "
                       "default constructor of such a class, because nothing "
                       "it could write would ever set that member. Give the "
                       "member an initialiser, or the class a constructor "
                       "that takes one");
    }
}

bool Parser::hasMemberInitialiser(const std::string &tag) const {
    const std::string prefix = tag + "::";
    std::map<std::string, std::size_t>::const_iterator it =
        memberInit_.lower_bound(prefix);
    return it != memberInit_.end() &&
           it->first.compare(0, prefix.size(), prefix) == 0;
}

std::vector<StmtPtr> Parser::memberInitialisers(const std::string &tag,
                                                const Type *type, int thisSlot,
                                                const std::set<std::string> &already,
                                                std::size_t pos) {
    std::vector<StmtPtr> out;
    const std::vector<Member> &ms = type->members();
    const std::set<const Type *> taken = unionsNamedBy(type, already);
    for (std::size_t i = 0; i < ms.size(); i++) {
        if (already.find(ms[i].name) != already.end()) continue;
        if (ms[i].anonUnion != nullptr && taken.count(ms[i].anonUnion) != 0) continue;
        StmtPtr one = memberInitialiser(tag, type, ms[i], thisSlot, pos);
        if (one != nullptr) out.push_back(std::move(one));
    }
    return out;
}

StmtPtr Parser::memberInitialiser(const std::string &tag, const Type *type,
                                  const Member &m, int thisSlot,
                                  std::size_t pos) {
    std::map<std::string, std::size_t>::const_iterator it =
        memberInit_.find(tag + "::" + m.name);
    if (it == memberInit_.end()) return nullptr;

    // Read where it was written, with the constructor's locals put aside and
    // the class current: an initialiser on a member is in the class's scope,
    // where its enumerators and static members answer unqualified.
    const std::size_t resume = at_;
    std::vector<Local> outer;
    outer.swap(locals_);
    // `this` stays ([class.mem]/4: the initialiser may name other members),
    // and it is this constructor's `this` - kept where it is the slot and
    // class here, made for them otherwise (else `this->a` read garbage).
    bool ownThis = false;
    for (const Local &l : outer)
        if (l.name == "this" && l.offset == thisSlot && l.type != nullptr && l.type->isPointer() &&
            l.type->pointee()->unqualified() == type->unqualified()) {
            locals_.push_back(l);
            ownThis = true;
        }
    if (!ownThis) locals_.push_back(Local{ "this", thisSlot, types_.pointerTo(type), false, std::string() });
    const Type *outerClass = currentClass_;
    currentClass_ = type;
    struct RestoreClass {
        Parser *p; const Type *was;
        ~RestoreClass() { p->currentClass_ = was; }
    } restoreClass{ this, outerClass };
    at_ = it->second;

    // `int a[3] = {1, 2, 3};`, `P p{4, 5};`: a list for an array or a class, [class.base.init]/9 with
    // [dcl.init.list]/3 - what `= {}` makes of a class or of an array that constructs is in it too.
    const Type *plainType = m.type->unqualified();
    if (peek().is("{") && !m.type->isReference() &&
        (plainType->isStructOrUnion() || (plainType->isArray() &&
                                          (needsConstruction(plainType) || !peekAt(1).is("}"))))) {
        const std::size_t listAt = peek().pos;
        Init in = parseInitialiser();
        StmtPtr made = listInitialiseMember(tag, type, m, thisSlot, in, listAt);
        locals_.swap(outer);
        at_ = resume;
        std::vector<StmtPtr> all;
        all.push_back(std::move(made));
        flushTemporaries(all);
        if (all.size() == 1) return std::move(all[0]);
        return StmtPtr(new Block(std::move(all)));
    }

    // `{v}` on a scalar: the value, which may not narrow - [dcl.init.list]/7.
    const bool braced = peek().is("{") && !peekAt(1).is("}") &&
        !(peekAt(1).kind == TokenKind::Num && !peekAt(1).isFloat && peekAt(1).value == 0 && peekAt(2).is("}"));
    // `= {}` or `= {0}`: the member's bytes zeroed by memset, an array or a
    // scalar alike, which is what [dcl.init.list]/3 comes to for either.
    if (peek().is("{") && !braced) {
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        ExprPtr field = thisMember(thisSlot, type, m);
        ExprPtr addr(new Unary('&', std::move(field)));
        addr->setType(types_.pointerTo(m.type));
        ExprPtr asVoid(new Cast(voidPtr, std::move(addr)));
        asVoid->setType(voidPtr);
        std::vector<ExprPtr> args;
        args.push_back(std::move(asVoid));
        ExprPtr zero(new Num(0LL));
        zero->setType(types_.intType());
        args.push_back(std::move(zero));
        ExprPtr n(new Num(static_cast<long long>(m.type->size(target_))));
        n->setType(types_.get(target_.sizeType()));
        args.push_back(std::move(n));
        std::vector<int> argSlots(args.size(), 0);
        Call *fill = new Call("memset", nullptr, std::move(args), false, 0, -1,
                              std::move(argSlots));
        fill->setSymbol("memset");
        ExprPtr filled(fill);
        filled->setType(voidPtr);
        locals_.swap(outer);
        at_ = resume;
        return StmtPtr(new ExprStmt(std::move(filled)));
    }
    if (braced) at_++;
    const std::size_t valueAt = peek().pos;
    ExprPtr value = decay(assign());
    if (braced) {
        checkNarrowing(m.type, *value, valueAt, "'" + m.name + "'");
        expect("}");
    }

    // **A class-typed member is *built* from its initialiser, not assigned
    // one.**
    StmtPtr made;
    if (memberClass(m.type) != nullptr && !m.type->isReference() &&
        hasConstructors(memberClass(m.type)->tag())) {
        std::vector<ExprPtr> one;
        one.push_back(std::move(value));
        made = constructMember(tag, type, m, thisSlot, one, pos, false);
        if (made == nullptr) value = std::move(one[0]);
    }
    if (made == nullptr) {
        checkAssignable(*value, m.type, pos, "'" + m.name + "'");
        value = convert(std::move(value), m.type);
        ExprPtr field = thisMember(thisSlot, type, m);
        ExprPtr store(new Assign(std::move(field), std::move(value)));
        store->setType(m.type);
        made = StmtPtr(new ExprStmt(std::move(store)));
    }
    locals_.swap(outer);
    at_ = resume;

    // **The initialiser is a full expression, and its temporaries die at the
    // end of it** - [class.temporary]/4, which here means before the next
    // member is built and not at the end of the constructor.
    std::vector<StmtPtr> all;
    all.push_back(std::move(made));
    flushTemporaries(all);
    if (all.size() == 1) return std::move(all[0]);
    return StmtPtr(new Block(std::move(all)));
}

// **A class-typed member is built, not left.** [class.base.init]/8: one the list does
// not name is default-initialised, and a written constructor used to leave it holding
// the stack. An array member is built by a loop, N being a property of the type.
StmtPtr Parser::constructMember(const std::string &cls, const Type *type,
                                const Member &m, int thisSlot,
                                std::vector<ExprPtr> &args, std::size_t pos,
                                bool implicit) {
    const Type *mc = memberClass(m.type);
    if (mc == nullptr || mc->tag().empty() || m.type->isReference()) return nullptr;
    const std::string key = constructorKey(mc->tag());
    if (overloadsOf(key) == nullptr) return nullptr;
    if (!args.empty() && m.type->isArray())
        src_.fail(pos, "'" + m.name + "' is an array, and an initialiser list "
                       "cannot say what to pass to each element of it");

    // **A trivially copyable member is copied, not constructed.**
    if (args.size() == 1 && !m.type->isArray() &&
        copyConstructorOf(mc) == nullptr && moveConstructorOf(mc) == nullptr) {
        const Type *at = args[0]->type();
        if (at != nullptr && memberClass(at) == mc) {
            ExprPtr dst = thisMember(thisSlot, type, m);
            dst->setType(m.type);
            ExprPtr store(new Assign(std::move(dst), std::move(args[0])));
            store->setType(m.type);
            return StmtPtr(new ExprStmt(std::move(store)));
        }
    }

    // **[class.copy]/31: `k(K(a))` builds the member where the temporary would have been** - the temporary's
    // constructor is given the member's address (1.78), where the copy or move it leaves out could be called.
    if (args.size() == 1 && !m.type->isArray() && freshTemporarySlot(*args[0], mc) != 0) {
        const Signature *sel = moveConstructorOf(mc);
        if (sel == nullptr) sel = copyConstructorOf(mc);
        if (sel != nullptr && !sel->deleted &&
            (sel->access == Access::Public || insideAccessOf(mc, sel->access) || isFriendOf(mc)))
            if (Call *built = temporaryConstruction(*args[0])) {
                const int slot = freshTemporarySlot(*args[0], mc);
                ExprPtr dst = thisMember(thisSlot, type, m);
                dst->setType(mc);
                ExprPtr at(new Unary('&', std::move(dst)));
                at->setType(types_.pointerTo(mc));
                built->setArg(0, std::move(at));
                adoptTemporary(slot);
                return StmtPtr(new ExprStmt(std::move(args[0])));
            }
    }

    // Held by value: reading a default argument can grow `functions_`.
    Signature chosen;
    if (!args.empty()) {
        chosen = resolveOverload(key, args, pos);
    } else {
        const Signature *ctor = defaultConstructorOf(mc);
        if (ctor == nullptr)
            src_.fail(pos, implicit
                ? "'" + cls + "' has no constructor of its own, and the one "
                  "the compiler would write cannot build its member '" +
                  m.name + "': '" + mc->tag() + "' has no constructor taking "
                  "nothing"
                : "this constructor of '" + cls + "' does not name '" + m.name +
                  "' in its initialiser list, and '" + mc->tag() + "' has no "
                  "constructor taking nothing - add ': " + m.name + "(...)' "
                  "to the list");
        chosen = *ctor;
    }
    if (chosen.access != Access::Public && !insideAccessOf(mc, chosen.access) &&
        !isFriendOf(mc))
        src_.fail(pos, implicit
            ? "'" + cls + "' cannot be built by the constructor the compiler "
              "would write: the constructor of '" + mc->tag() + "' taking "
              "nothing is " + (chosen.access == Access::Private ? "private"
                                                                 : "protected")
            : "'" + mc->tag() + "' has no public constructor taking these "
              "arguments for '" + m.name + "' - the one that matches is " +
              (chosen.access == Access::Private ? "private" : "protected"));
    for (std::size_t k = 0; k < functions_.size(); k++)
        if (functions_[k].symbol == chosen.symbol) functions_[k].used = true;
    applyDefaults(chosen, args, pos);

    int indexSlot = 0;
    long long count = 0;
    if (m.type->isArray()) {
        memberElements(m.type, &count);
        if (count < 0)
            src_.fail(pos, "'" + cls + "::" + m.name + "' has no length, so a "
                           "constructor does not know how many elements to "
                           "build");
        indexSlot = allocateFrameSlot(types_.intType());
    }

    ExprPtr acc = thisMember(thisSlot, type, m);

    ExprPtr addr;
    if (m.type->isArray()) {
        addr = indexBytes(types_, decay(std::move(acc)), mc, indexSlot, target_);
    } else {
        addr = ExprPtr(new Unary('&', std::move(acc)));
        addr->setType(types_.pointerTo(mc));
    }

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    std::vector<const Type *> ps;
    ps.push_back(types_.pointerTo(mc));
    for (std::size_t k = 0; k < args.size(); k++) {
        all.push_back(std::move(args[k]));
        ps.push_back(chosen.params[k]);
    }
    StmtPtr one(new ExprStmt(
        completeCall(mc->tag(), chosen.symbol, nullptr, types_.get(Kind::Void),
                     ps, false, pos, std::move(all))));
    if (!m.type->isArray()) return one;
    Alive built{ "this", thisSlot, mc };
    built.owner = type;
    for (std::size_t k = 0; k < type->members().size(); k++)
        if (&type->members()[k] == &m) built.member = static_cast<int>(k);
    StmtPtr loop = eachElement(indexSlot, count, std::move(one));
    return built.member < 0 ? std::move(loop) : guardElements(std::move(loop), built, indexSlot, pos);
}

const Type *Parser::parameterAsWritten(const Type *declared,
                                       const Type *adjusted) {
    if (!adjusted->isPointer()) return adjusted;
    // An array of const elements is a const *array*, so isConst() answers for
    // `const int a[]` as well - and both want the same const pointer.
    if (declared->isArray() || declared->isConst())
        return types_.withConst(adjusted);
    return adjusted;
}

// **A function type is interned on its parameters, so two functions with one
// signature are one type** - which is why the written list cannot be kept on
// it: `f1(int a[])` and `f2(int *a)` would then be the same Q.
const Type *Parser::manglingType(const Type *fn) {
    if (writtenParams_.empty() || writtenFor_ != fn->params()) return fn;
    return types_.functionType(fn->returns(), writtenParams_, fn->isVariadicFn());
}

// **[dcl.fct]/5: a parameter declared with a function type is a pointer to that function** -
// `int apply(int c(int), int x)` was "expected ','" (R9). The list read here is the parameter's own,
// so the one it sits in keeps its defaults and written types.
const Type *Parser::functionTypeParameter(const Type *returns) {
    std::vector<std::size_t> defaults;
    defaults.swap(pendingDefaults_);
    std::vector<const Type *> written, writtenFor;
    written.swap(writtenParams_);
    writtenFor.swap(writtenFor_);
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    fnPointerSpec_ = skipExceptionSpecification();
    pendingDefaults_.swap(defaults);
    writtenParams_.swap(written);
    writtenFor_.swap(writtenFor);
    return types_.pointerTo(types_.functionType(returns, std::move(params), variadic));
}

// [dcl.ambig.res]: `int (int)` in a parameter list is an unnamed parameter of function type, where
// the parentheses could have been a declarator's - a type name after them says which.
Parser::Declared Parser::parameterDeclarator(const Type *base) {
    if (peek().is("(")) {
        at_++;
        bool list = peek().is(")") || peek().is("...") || atTypeName();
        // `S (T::*p)()` names a class on its way to a member pointer.
        int depth = 0;
        for (std::size_t i = at_; list && i + 1 < tokens_.size(); i++) {
            const Token &t = tokens_[i];
            if (t.is("(") || t.is("<")) depth++;
            else if (t.is(">")) depth--;
            else if (t.is(">>")) depth -= 2;
            else if (t.is(")") && depth-- == 0) break;
            else if (t.is(";") || t.is("{") || t.kind == TokenKind::End) break;
            else if (depth == 0 && t.is("::") && tokens_[i + 1].is("*")) list = false;
        }
        at_--;
        if (list) return Declared{ std::string(), base, peek().pos, 0, std::string() };
    }
    return declarator(base, true);
}

// **`noexcept`, `throw()` or `throw(T)` on a function type that is no
// declaration** (a function pointer, parameter, typedef), [except.spec]/2:
// read and set aside, no part of the type in C++11 (was `expected ';'`).
int Parser::skipExceptionSpecification() {
    if (!peek().is("noexcept") && !(peek().is("throw") && peekAt(1).is("("))) return 0;
    // **Except in a typedef or an alias declaration, where [except.spec]/2 forbids it**: the
    // declaration this stands in is found by walking back to where it began.
    int depth = 0;
    for (std::size_t k = at_; k-- > 0; ) {
        const Token &t = tokens_[k];
        if (t.is(")") || t.is("]")) depth++;
        else if (t.is("(") || t.is("[")) depth--;
        else if (depth <= 0 && (t.is(";") || t.is("{") || t.is("}"))) break;
        else if (depth <= 0 && (t.is("typedef") ||
                                (t.is("using") && k + 2 < at_ && tokens_[k + 2].is("="))))
            src_.fail(peek().pos, "an exception specification cannot be written in a " +
                                  std::string(t.is("typedef") ? "typedef" : "alias declaration") +
                                  " - [except.spec]/2; put it on the functions themselves");
    }
    const DynamicSpec held = pendingSpec_;
    const bool nothrow = exceptionSpecification();
    const int code = nothrow ? 1 : pendingSpec_.written ? 2 : 0;   // noexcept(false) lets all through
    pendingSpec_ = held;
    return code;
}

// [dcl.fct.default]/7-9 over one default argument's tokens [from, to): no
// parameter, enclosing local, `this` or non-static member is read (but as
// `x.m` or in sizeof/alignof/decltype/noexcept); `cls`/`locals` skip some.
void Parser::checkDefaultArgument(std::size_t from, std::size_t to, const std::vector<std::string> &params,
                                  const Type *cls, bool locals) {
    for (std::size_t i = from; i < to; i++) {
        const Token &t = tokens_[i];
        if ((t.is("sizeof") || t.is("alignof") || t.is("decltype") || t.is("noexcept")) &&
            i + 1 < to && tokens_[i + 1].is("(")) {
            int depth = 0;
            for (i++; i < to; i++) {
                if (tokens_[i].is("(")) depth++;
                else if (tokens_[i].is(")") && --depth == 0) break;
            }
            continue;
        }
        if (t.is("this"))
            src_.fail(t.pos, "'this' cannot be used in a default argument - [dcl.fct.default]/8");
        if (t.kind != TokenKind::Ident || (i > from && (tokens_[i - 1].is(".") || tokens_[i - 1].is("->") ||
                                                        tokens_[i - 1].is("::"))) ||
            (i + 1 < to && tokens_[i + 1].is("::")))
            continue;
        if (std::find(params.begin(), params.end(), t.text) != params.end())
            src_.fail(t.pos, "'" + t.text + "' is a parameter, and a default argument cannot read one - "
                             "[dcl.fct.default]/9");
        if (const Local *l = locals ? findLocal(t.text) : nullptr)
            if (!l->constantOnly)
                src_.fail(t.pos, "'" + t.text + "' is a local variable, and a default argument cannot read "
                                 "one - [dcl.fct.default]/7");
        if (cls != nullptr && cls->findMember(t.text) != nullptr)
            src_.fail(t.pos, "'" + t.text + "' is a non-static member, and a default argument names one only "
                             "through an object - [dcl.fct.default]/9");
    }
}

// The member half of the rule above, asked of the defaults written in the class's body once its members are known.
void Parser::checkMemberDefaults(const Type *type) {
    std::vector<MemberDefault> rest;
    for (const MemberDefault &m : memberDefaults_) {
        if (m.cls == type) checkDefaultArgument(m.from, m.to, std::vector<std::string>(), type, false);
        else rest.push_back(m);
    }
    memberDefaults_.swap(rest);
}

void Parser::parameterTypes(std::vector<const Type *> &params, bool &variadic) {
    expect("(");
    variadic = false;
    pendingDefaults_.clear();
    writtenParams_.clear();
    writtenFor_.clear();
    std::vector<const Type *> written;
    std::vector<std::string> names;
    std::size_t closed = peek().pos;
    if (consume(")")) return;
    if (peek().is("void") && peekAt(1).is(")")) { at_ += 2; return; }

    for (;;) {
        if (consume("...")) { variadic = true; expect(")"); break; }
        if (packParameter(&params, nullptr)) {
            // A pack's members are their own types; keep the two lists level.
            while (written.size() < params.size()) written.push_back(params[written.size()]);
            if (consume(")")) break;
            expect(",");
            continue;
        }
        StorageClass psc;
        Qualifiers pquals;
        const Type *pt = specifiers(&psc, &pquals);
        Declared pd = parameterDeclarator(pt);
        if (peek().is("(")) pd.type = functionTypeParameter(pd.type);
        refuseThreadLocalHere(pquals.isThreadLocal || pquals.threadLocalAsStatic, pd.pos);
        if (mentionsDeduced(pd.type))
            src_.fail(pd.pos, "a parameter's type cannot be deduced - `auto` "
                              "there is C++14, and this compiler is C++11");
        const Type *declared = pd.type;
        if (pd.type->isArray()) pd.type = types_.pointerTo(pd.type->pointee());
        if (pd.type->isVoid())
            src_.fail(pd.pos, "'void' is only a parameter list on its own");
        params.push_back(types_.withoutConst(pd.type));
        written.push_back(parameterAsWritten(declared, params.back()));
        if (!pd.name.empty()) names.push_back(pd.name);

        // `int b = 3`. The tokens are left where they are and their position
        // recorded; a call that omits the argument reads them again.
        pendingDefaults_.resize(params.size(), 0);
        if (consume("=")) {
            pendingDefaults_.back() = at_;
            skipDefaultArgument();
            if (at_ == pendingDefaults_.back())
                src_.fail(peek().pos, "this parameter says '=' and then gives "
                                      "no default");
            checkDefaultArgument(pendingDefaults_.back(), at_, names, nullptr,
                                 classStack_.empty() && !currentFunction_.empty());
            if (!classStack_.empty())
                memberDefaults_.push_back(MemberDefault{ classStack_.back(), pendingDefaults_.back(), at_ });
        }
        closed = peek().pos;
        if (consume(")")) break;
        expect(",");
    }

    // [dcl.fct.default]/4 is asked of every declaration's defaults together, where they are stored:
    // `int f(int, int = 2, int);` after `int f(int, int, int = 3);` is whole (R8).
    pendingDefaultsAt_ = closed;
    if (written.size() == params.size() && written != params) {
        writtenParams_ = std::move(written);
        writtenFor_ = params;
    }
}

// [dcl.fct.default]/4: once a parameter has a default, every one after it must have
// one too - a call fills them in from the right, so a parameter without one behind a
// parameter with one could never be reached. Said where the list is read.
void Parser::requireDefaultsAreASuffix(const std::vector<std::size_t> &defaults,
                                       std::size_t pos) {
    bool seen = false;
    for (std::size_t i = 0; i < defaults.size(); i++) {
        if (defaults[i] != 0) { seen = true; continue; }
        if (seen)
            src_.fail(pos, "every parameter after one with a default needs a "
                           "default of its own - a call fills them in from the "
                           "right, so there would be no way to reach this one");
    }
}

