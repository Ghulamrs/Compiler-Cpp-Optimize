// The parser: what a class needs written for it. Constructors and destructors,
// vtables and thunks, the implicit special members and the code that defines them
// when something calls one, and the static data members. Rungs 3 and 4.
#include <cstdio>
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

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
    const std::string key = constructorKey(d.type->tag());
    const Signature &ctor = resolveOverload(key, args, d.pos);
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
                       const std::vector<const Type *> &params, bool constThis) {
    if (s.name != name || s.constThis != constThis) return false;
    return sameParameters(s.params, params);
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

void Parser::declareDestructor(const std::string &cls, std::size_t pos,
                               Access access, bool isVirtual) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    if (!params.empty() || variadic)
        src_.fail(pos, "a destructor takes no parameters");
    // A destructor is `noexcept` in C++11 whether or not it says so.
    pendingNoexcept_ = exceptionSpecification();

    if (overloadsOf(destructorKey(cls)) != nullptr)
        src_.fail(pos, "'" + cls + "' has two destructors, and a class has one");
    registerDestructor(cls, pos, access, isVirtual, false);
}

// Everything a destructor needs in the tables, whether a program wrote it or
// the compiler did: the name, the entry, and the vtable slots a virtual one
// claims.
void Parser::registerDestructor(const std::string &cls, std::size_t pos,
                                Access access, bool isVirtual, bool implicit) {
    const std::vector<const Type *> params;
    const std::string key = destructorKey(cls);

    // **A base with a virtual destructor makes this one virtual**, keyword or not -
    // [class.dtor]. The base's slots are already in this class's table, so it is
    // answered by looking for the "~" entry, and before the name is built.
    std::vector<VSlot> &slots = vtables_[cls];
    std::size_t slot = slots.size();
    for (std::size_t i = 0; i < slots.size(); i++)
        if (slots[i].name == "~") { slot = i; isVirtual = true; break; }
    // And a base off the primary chain - a second base, a virtual one - whose
    // destructor is virtual makes this one virtual too, in a slot of its own.
    if (!isVirtual)
        if (const Type *self = findTypedef(cls)) {
            const std::vector<Type::BaseSpec> &bs = self->bases();
            for (std::size_t bi = 0; bi < bs.size() && !isVirtual; bi++) {
                std::map<std::string, std::vector<VSlot> >::const_iterator it =
                    vtables_.find(bs[bi].type->tag());
                if (it == vtables_.end()) continue;
                for (std::size_t i = 0; i < it->second.size(); i++)
                    if (it->second[i].name == "~") { isVirtual = true; break; }
            }
        }

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

    // **A virtual destructor claims slots where it is declared**, and how many
    // depends on the ABI: Itanium two and adjacent, Microsoft one holding only the
    // deleting form. A derived class overrides in place, matching on "~".
    if (!isVirtual) return;
    const bool ms = target_.microsoftNames();
    const std::string deleting = deletingDestructorSymbol(cls);

    std::vector<const Type *> none;
    if (slot < slots.size()) {
        slots[slot].symbol = ms ? deleting : out;       // the complete form
        if (!ms && slot + 1 < slots.size() &&
            slots[slot + 1].name == "~$deleting")
            slots[slot + 1].symbol = deleting;
        return;
    }
    slots.push_back(VSlot{ "~", ms ? deleting : out, none, false });
    if (!ms) slots.push_back(VSlot{ "~$deleting", deleting, none, false });
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
        const std::string symbol =
            ptrs.size() == 1 || ptrs[p].base == nullptr
                ? vbtableSymbol(cls)
                : vbtableSymbol(cls, ptrs[p].base->tag());

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

    // **The table's ADDRESS, not its contents.** A global Var is an lvalue and
    // reading one loads from it, which stored the table's first word in the vptr and
    // crashed on the first call. Giving it the array type and decaying it is it.
    const std::size_t entryCount = vtables_[cls].size();
    ExprPtr base(Var::global(table));
    base->setType(types_.arrayOf(entry, static_cast<long long>(entryCount)));
    ExprPtr value = decay(std::move(base));
    ExprPtr asVoid(new Cast(entry, std::move(value)));
    asVoid->setType(entry);

    ExprPtr self(Var::local("this", thisSlot));
    self->setType(entries);                        // the vptr lives at offset 0
    ExprPtr where(new Unary('*', std::move(self)));
    where->setType(entry);

    ExprPtr store(new Assign(std::move(where), std::move(asVoid)));
    store->setType(entry);

    withVptr.push_back(StmtPtr(new ExprStmt(std::move(store))));
    return withVptr;
}

// **A function with no source behind it.** The deleting destructor runs the
// destructor and then gives the memory back, because `delete p` through a base
// pointer reaches both through one slot. Itanium's D0 and Microsoft's ??_G differ.
void Parser::synthesizeDeleting(const std::string &cls, const Type *type,
                                Access access, std::size_t pos) {
    const bool ms = target_.microsoftNames();
    const Type *self = types_.pointerTo(type);
    const std::string symbol = deletingDestructorSymbol(cls);
    (void)access;

    // Its own frame: `this`, and on Windows the flag beside it.
    const int savedFrame = frameSize_;
    frameSize_ = 0;
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
    current_->functions.back().setSymbol(symbol);
    // **A compiler-written special member is inline** - [class.copy] and its
    // neighbours say the implicit definition is - so several translation units
    // may each hold one and the linker folds them.
    current_->functions.back().setInline(true);
    frameSize_ = savedFrame;
}

// **What cl's most-derived flag guards**: the vbtable pointers this class
// stores and the virtual bases it builds.
std::vector<StmtPtr> Parser::guardedVirtualBaseInit(
        const Type *type, const std::string &cls, int thisSlot, int flagSlot,
        std::size_t pos, int srcSlot, bool moving,
        std::map<std::string, std::vector<ExprPtr> > *vbaseArgs) {
    std::vector<StmtPtr> out;
    if (!target_.microsoftNames() || !type->hasVirtualBase() || flagSlot < 0)
        return out;

    std::vector<StmtPtr> inside;
    storeVbptr(cls, type, thisSlot, inside);
    std::vector<StmtPtr> built = virtualBaseCalls(type, thisSlot, true, pos,
                                                  srcSlot, moving, vbaseArgs);
    for (std::size_t i = 0; i < built.size(); i++)
        inside.push_back(std::move(built[i]));
    if (inside.empty()) return out;

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
                                                  *vbaseArgs) {
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
    for (std::size_t i = 0; i < found.size(); i++) {
        const Type *b = found[i]->type;
        const std::vector<Type::BaseSpec> &all = type->bases();
        for (std::size_t k = 0; k < all.size(); k++)
            if (all[k].isVirtual && all[k].type == b) bs.push_back(all[k]);
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
                const Type *fnType =
                    types_.functionType(types_.get(Kind::Void), ctor->params,
                                        false);
                std::string sub, why;
                if (!itaniumConstructorName(base->tag(), base, fnType, false,
                                            &sub, &why))
                    continue;
                symbol = sub;
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
    }
    return out;
}

void Parser::synthesizeCompleteCtor(const Type *type,
                                    const std::vector<const Type *> &ctorParams,
                                    const std::string &c1, const std::string &c2,
                                    bool isInline, std::size_t pos,
                                    int copyArg, bool moving,
                                    std::map<std::string,
                                        std::vector<ExprPtr> > *vbaseArgs) {
    const std::string &cls = type->tag();
    const Type *self = types_.pointerTo(type);

    const int savedFrame = frameSize_;
    frameSize_ = 0;
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
    std::vector<StmtPtr> body = virtualBaseCalls(type, thisSlot, true, pos,
                                                 srcSlot, moving, vbaseArgs);

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
    body.push_back(StmtPtr(new ExprStmt(
        completeCall(cls, c2, nullptr, types_.get(Kind::Void), ps, false, pos,
                     std::move(args)))));

    current_->functions.push_back(Function(cls + "::complete",
                                           types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos,
                                           std::vector<::Local>()));
    current_->functions.back().setSymbol(c1);
    if (isInline) current_->functions.back().setInline(true);
    frameSize_ = savedFrame;
}

void Parser::synthesizeCompleteDtor(const Type *type, const std::string &d1,
                                    const std::string &d2, bool isInline,
                                    std::size_t pos) {
    const std::string &cls = type->tag();
    const Type *self = types_.pointerTo(type);

    const int savedFrame = frameSize_;
    frameSize_ = 0;
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });

    // D2 first - it destroys what C2 built - and the virtual bases after it.
    std::vector<StmtPtr> body;
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
    frameSize_ = savedFrame;
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
    // Calling one is what asks for a body, which is the only thing that makes
    // an implicit destructor a function at all.
    markUsed(&dtor);
    std::vector<ExprPtr> args;
    args.push_back(std::move(address));
    std::vector<const Type *> params;
    params.push_back(args[0]->type());
    // **A complete object of a Microsoft class with a virtual base is
    // destroyed through `??_D`**, which destroys the class's own part and then
    // the virtual bases - Itanium's D1.
    std::string symbol = dtor.symbol;
    if (target_.microsoftNames() && msVbaseClasses_.count(dtor.owner) != 0) {
        symbol = vbaseDestructorSymbol(dtor.owner);
        markSymbolUsed(symbol);
    }
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
    const int helpSlot = allocateFrameSlot(voidPtr);
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
                    cleanupPad(handOver == tryChainLabel_ && intoTry
                                   ? tryChainAliveFrom_ : aliveAtEntry,
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
    both.push_back(StmtPtr(new ExprStmt(setGuard(t.flag, 0))));
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

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    std::vector<ExprPtr> args;
    ExprPtr ptr(Var::local(".ex.ptr", pointerSlot));
    ptr->setType(voidPtr);
    args.push_back(std::move(ptr));
    // **A segment of a `try` body hands over rather than resuming.** Its row
    // carries the `try`'s catch types, so the selector may name a handler -
    // and the one chain that tests it lives in the `try`'s own pad.
    if (chainLabel.empty())
        steps.push_back(StmtPtr(new ExprStmt(
            resumeCall(
                        std::move(args)))));
    else
        steps.push_back(StmtPtr(new Goto(chainLabel)));
    return unwindPad(std::move(steps));
}

ExprPtr Parser::resumeCall(std::vector<ExprPtr> args) {
    if (target_.resumeTakesException())
        return runtimeCall("_Unwind_Resume", types_.get(Kind::Void), std::move(args));
    return runtimeCall("__cxa_end_cleanup", types_.get(Kind::Void), std::vector<ExprPtr>());
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
                                    std::size_t pos) {
    const bool ms = target_.microsoftNames();
    const std::string name = ms
        ? slot.symbol + "$adj" + std::to_string(offset)
        // _ZThn16_N1C1gEv.
        : "_ZThn" + std::to_string(offset) + "_" + slot.symbol.substr(2);
    // One per name: a construction vtable names the same thunk again.
    for (std::size_t i = 0; i < current_->functions.size(); i++)
        if (current_->functions[i].symbol() == name) return name;

    const Type *self = types_.pointerTo(type);
    const int savedFrame = frameSize_;
    frameSize_ = 0;

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
    const Type *returns = target != nullptr ? target->returns : types_.get(Kind::Void);

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
    frameSize_ = savedFrame;
    return name;
}

void Parser::markSymbolUsed(const std::string &symbol) {
    if (symbol.empty()) return;
    for (std::size_t i = 0; i < functions_.size(); i++)
        if (functions_[i].symbol == symbol) { functions_[i].used = true; return; }
}

void Parser::markUsed(const Signature *f) {
    functions_[static_cast<std::size_t>(f - &functions_[0])].used = true;
}

bool Parser::sameParameters(const std::vector<const Type *> &a,
                            const std::vector<const Type *> &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++)
        if (a[i] != b[i]) return false;
    return true;
}

bool Parser::memberFromBase(const Type *cls, const Member &m) {
    const std::vector<Type::BaseSpec> &bs = cls->bases();
    for (std::size_t k = 0; k < bs.size(); k++)
        if (m.offset >= bs[k].offset &&
            m.offset < bs[k].offset + bs[k].type->dataSize())
            return true;
    return false;
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

// **Which entry of the vbtable holds this base**, and where the base sits in
// the complete object. Entry 0 is the vbptr's own offset back to the top, so
// the first virtual base is entry 1 and a zero answer means "not there".
int Parser::virtualBaseSlot(const Type *owner, const Type *vbase, int *baseAt) {
    const std::vector<Type::BaseSpec> &bs = owner->bases();
    int seen = 0;
    for (std::size_t i = 0; i < bs.size(); i++) {
        if (!bs[i].isVirtual) continue;
        seen++;
        if (bs[i].type == vbase) {
            if (baseAt != nullptr) *baseAt = bs[i].offset;
            return seen;
        }
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
    const Type *offType = wordOffsetType();

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

// **A pointer's type_info is a `__pointer_type_info`** - [ABI 2.9.5]: vptr,
// name, the pointee's cv-qualifiers as flags, the pointee's own type_info.
// One to a fundamental type - `_ZTIPi`, `_ZTIPKc` - is the library's.
std::string Parser::emitPointerTypeInfo(const Type *ptr, std::size_t pos,
                                        std::string *why) {
    const Type *pointee = ptr->pointee();
    const Type *plain = pointee->unqualified();
    if (plain->isFunction() || plain->isMemberPointer() || plain->isMemberFunctionPointer()) {
        *why = "a pointer to a function has no type_info this compiler can "
               "emit - '" + ptr->describe() + "' would need a "
               "__function_type_info for what it points at";
        return std::string();
    }
    std::string spelt;
    if (!itaniumTypeSpelling(ptr, &spelt, why)) return std::string();
    const bool builtin = itaniumBuiltinCode(plain->kind()) != nullptr;
    if (builtin) return "_ZTI" + spelt;                   // the library's

    const std::string ti = "_ZTI" + spelt;
    for (std::size_t i = 0; i < current_->globals.size(); i++)
        if (current_->globals[i].symbol == ti) return ti;
    const std::string inner = typeInfoSymbolFor(plain, pos, why);
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

    // vptr, name, flags (const 1, volatile 2 - only const is in this type
    // system), the pointee's type_info at the next pointer boundary.
    const int w = pointerBytes();
    std::vector<GlobalPiece> pieces;
    pieces.push_back(GlobalPiece{ 0, w, 2 * w, "_ZTVN10__cxxabiv119__pointer_type_infoE" });
    pieces.push_back(GlobalPiece{ w, w, 0, ts });
    pieces.push_back(GlobalPiece{ 2 * w, 4, pointee->isConst() ? 1 : 0, std::string() });
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

    const int w = pointerBytes();
    std::vector<GlobalPiece> pieces;
    pieces.push_back(GlobalPiece{
        0, w, 2 * w,
        bases.empty() ? "_ZTVN10__cxxabiv117__class_type_infoE"
                      : simple ? "_ZTVN10__cxxabiv120__si_class_type_infoE"
                               : "_ZTVN10__cxxabiv121__vmi_class_type_infoE" });
    pieces.push_back(GlobalPiece{ w, w, 0, ts });
    int words = 2;
    if (simple) {
        pieces.push_back(GlobalPiece{ 2 * w, w, 0, baseTypeInfo[0] });
        words = 3;
    } else if (!bases.empty()) {
        // Two `unsigned int` in the third word - the class's flags and the
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
        words = 3 + static_cast<int>(bases.size()) * 2;
    }

    const Type *word = types_.pointerTo(types_.get(Kind::Void));
    const Type *object = types_.arrayOf(word, static_cast<long long>(words));
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
        const std::string symbol =
            ptrs.size() == 1 || ptrs[p].base == nullptr
                ? vbtableSymbol(tag)
                : vbtableSymbol(tag, ptrs[p].base->tag());
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

        // **Which bases the table names, and in whose order.**
        const std::vector<Type::BaseSpec> &bs = plain->bases();
        std::vector<const Type *> want;
        if (p != 0 && ptrs[p].base != nullptr) {
            const std::vector<Type::BaseSpec> &theirs =
                ptrs[p].base->bases();
            for (std::size_t i = 0; i < theirs.size(); i++)
                if (theirs[i].isVirtual) want.push_back(theirs[i].type);
        } else {
            for (std::size_t i = 0; i < bs.size(); i++)
                if (bs[i].isVirtual) want.push_back(bs[i].type);
        }

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

    const int w = pointerBytes();
    std::vector<GlobalPiece> pieces;
    int at = 0;
    for (std::size_t i = 0; i < slots.size(); i++) {
        pieces.push_back(GlobalPiece{ at, w, 0, slots[i].symbol });
        at += w;
    }

    // **The Microsoft ABI arranges a second polymorphic base differently,
    // and it is not the same thing under other names.** Measured with
    // clang: two vftable symbols rather than one table in two parts.
    const std::vector<Type::BaseSpec> &bases = cls->bases();
    for (std::size_t bi = 1; bi < bases.size(); bi++) {
        const Type *b = bases[bi].type;
        if (!b->hasVptr() || bases[bi].isVirtual) continue;
        if (b->polymorphic())
            src_.fail(pos, "'" + tag + "' has virtual functions in a base "
                           "that is not the first, and the Microsoft ABI "
                           "lays that out differently - two vftable "
                           "symbols rather than one table in two parts. "
                           "Not supported yet; it is measured for Itanium "
                           "only");
    }

    // **The Microsoft locator goes in front of the table, not behind it.**
    std::string locatorWord;
    if (cls->bases().size() <= 1) {
        MicrosoftRtti names;
        std::string why;
        if (microsoftClassRttiNames(cls, &names, &why)) {
            locatorWord = names.locator;
            bool had = false;
            for (std::size_t i = 0; i < current_->rtti.size(); i++)
                if (current_->rtti[i] == cls) had = true;
            if (!had) current_->rtti.push_back(cls);
        }
    }

    const Type *entry = types_.pointerTo(types_.get(Kind::Void));
    const Type *table = types_.arrayOf(entry, static_cast<long long>(pieces.size()));
    current_->globals.push_back(Global{ symbol, symbol, table, std::move(pieces),
                                        true, false, true, locatorWord, true });
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
    // Read here rather than at the call site.
    pendingNoexcept_ = exceptionSpecification();

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
    if (!pendingDefaults_.empty()) defaultArgs_[out] = pendingDefaults_;
    if (!pendingDefaults_.empty())
        defaultArgNamespace_[out] = namespaceStack_;
    pendingDefaults_.clear();
    functions_.push_back(Signature{ cls, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, cls, false, access });
    functions_.back().isExplicit = isExplicit;
    functions_.back().isNoexcept = pendingNoexcept_;
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
        if (c.params.size() != 1) continue;
        const Type *p = c.params[0]->unqualified();
        if (p->isStructOrUnion() && p->isSpecialization() &&
            p->templateName() == "initializer_list" &&
            (p->templateNamespace() == "std" ||
             p->templateNamespace() == "std::")) {
            if (ilType != nullptr) *ilType = c.params[0];
            return &c;
        }
    }
    return nullptr;
}

// **A brace-init-list becomes a backing array and an initializer_list over
// it**, [dcl.init.list]/5. The array holds the elements; the list is `{
// &array[0], count }` - the pointer and count <initializer_list> lays out.
ExprPtr Parser::buildInitializerList(const Type *ilType, Init &in,
                                     std::size_t pos, std::vector<StmtPtr> &into) {
    const Type *elem = ilType->templateArgs().empty()
                         ? types_.get(Kind::Int)
                         : ilType->templateArgs()[0].type;
    const long long n = static_cast<long long>(in.items.size());
    const int k = ilTemps_++;

    const Type *arrType = types_.arrayOf(elem, n);
    const std::string bk = ".ilbacking" + std::to_string(k);
    const int bkOff = declare(bk, arrType, pos);

    // **A class element with an initializer_list constructor is built through
    // it, not aggregate-initialised.**
    const Type *plainElem = elem->unqualified();
    const Type *innerIl = nullptr;
    const Signature *innerCtor = plainElem->isStructOrUnion()
        ? initializerListConstructor(plainElem, &innerIl) : nullptr;
    if (innerCtor != nullptr) {
        markUsed(innerCtor);
        const std::string sym = innerCtor->symbol;
        const Type *innerParam = innerCtor->params[0];
        const Type *elemPtr = types_.pointerTo(plainElem);
        for (std::size_t i = 0; i < in.items.size(); i++) {
            if (!in.items[i].isList)
                src_.fail(in.items[i].pos, "this element is built from a braced "
                          "list and this initialiser is not braced");
            ExprPtr list = buildInitializerList(innerIl, in.items[i], pos, into);
            ExprPtr arr(Var::local(bk, bkOff)); arr->setType(arrType);
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
    ExprPtr arr(Var::local(bk, bkOff)); arr->setType(arrType);
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
    return eachElement(indexSlot, count, std::move(one));
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
    const int savedFrame = frameSize_;
    frameSize_ = 0;
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
    body.push_back(eachElement(indexSlot, convert(std::move(n), types_.intType()), std::move(one)));
    body.push_back(StmtPtr(new Return(nullptr)));
    current_->functions.push_back(Function(name, types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), true, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(name);
    frameSize_ = savedFrame;
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
    const int savedFrame = frameSize_;
    frameSize_ = 0;
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
    body.push_back(StmtPtr(new While(std::move(cond), StmtPtr(new Block(std::move(inner))))));
    body.push_back(StmtPtr(new Return(nullptr)));
    current_->functions.push_back(Function(name, types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), true, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(name);
    frameSize_ = savedFrame;
    return name;
}

// [expr.new]/12 as the Itanium ABI fixes it: a class delete[] must destroy
// keeps its count in the last size_t of a cookie in front of the array, as
// wide as the larger of size_t and the element's alignment; else none.
int Parser::arrayCookie(const Type *elem) const {
    if (destructorOf(elem->unqualified()) == nullptr) return 0;
    const int sizeT = types_.get(target_.sizeType())->size(target_);
    const int align = elem->align(target_);
    return align > sizeT ? align : sizeT;
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

const Parser::Signature *Parser::copyAssignOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(assignmentKey(cls->tag()));
    return set == nullptr ? nullptr : &functions_[(*set)[0]];
}

std::string Parser::baseConstructorSymbol(const Signature &ctor, const Type *base) {
    if (target_.microsoftNames()) return ctor.symbol;
    const Type *fnType = types_.functionType(types_.get(Kind::Void), ctor.params,
                                             false);
    std::string sub, why;
    if (itaniumConstructorName(base->tag(), base, fnType, false, &sub, &why))
        return sub;
    return ctor.symbol;
}

// **A trivial special member is not a function**, and that is measured rather than
// reasoned: cl and clang emit no symbol for one with no work to do. So an implicit
// member is declared only where it has some - a vptr, or a base or member to build.
void Parser::declareImplicitSpecials(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (tag.empty() || type->kind() == Kind::Union) return;
    // Asked before the copy constructor is declared, because declaring one would answer it yes.
    const bool wroteConstructor = overloadsOf(constructorKey(tag)) != nullptr;
    // **Read now, for the same reason and at the same moment.** After the three calls
    // below, every one of these answers yes for a class that wrote nothing at all,
    // and [class.copy]/9 is a question about what the *user* declared.
    const bool wroteCopyOrDtor = copyConstructorOf(type) != nullptr ||
                                 moveConstructorOf(type) != nullptr ||
                                 overloadsOf(destructorKey(tag)) != nullptr;
    declareImplicitDestructor(tag, type, pos);
    declareImplicitCopyCtor(tag, type, pos);
    declareImplicitCopyAssign(tag, type, pos);
    // Before the `wroteConstructor` return below: writing a constructor of
    // your own costs you the implicit *default* one and nothing else.
    declareImplicitMoveCtor(tag, type, pos, wroteCopyOrDtor);
    if (wroteConstructor) return;

    // **An initialiser on a member is work**, and this is where a class with nothing but `int x =
    // 5;` gets a default constructor at all: without one there is no function to put the store in,
    // and `S s;` would leave x holding the stack.
    bool work = type->hasVptr();
    for (std::size_t i = 0; i < type->members().size() && !work; i++)
        if (memberInit_.find(tag + "::" + type->members()[i].name) !=
            memberInit_.end())
            work = true;
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (!bs[i].type->tag().empty() &&
            overloadsOf(constructorKey(bs[i].type->tag())) != nullptr)
            work = true;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++) {
        const Type *mc = memberClass(ms[i].type);
        if (mc != nullptr && !mc->tag().empty() &&
            overloadsOf(constructorKey(mc->tag())) != nullptr)
            work = true;
    }
    if (!work) return;

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
                                    false, false, pos, false, tag, false,
                                    Access::Public, false });
    functions_.back().implicit = true;
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
    if (!work) return;

    registerDestructor(tag, pos, Access::Public, isVirtual, true);
}

// Its body: the members this class added, in the reverse of the order they
// were declared, then the bases in the reverse of theirs.
std::vector<StmtPtr> Parser::memberDestructors(const std::string &cls,
                                               const Type *type, int thisSlot,
                                               std::size_t pos) {
    std::vector<StmtPtr> out;
    const std::vector<Member> &ms = type->members();
    for (std::size_t n = ms.size(); n-- > 0; ) {
        if (memberFromBase(type, ms[n])) continue;

        const Type *mt = ms[n].type;
        long long elemCount = 1;
        const Type *elem = mt->isArray() ? memberElements(mt, &elemCount) : mt;
        const Signature *dtor = destructorOf(memberClass(mt));
        if (dtor == nullptr) continue;
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

    return out;
}

void Parser::synthesizeDestructor(std::size_t which) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const bool isVirtual = functions_[which].isVirtual;
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const int savedFrame = frameSize_;
    frameSize_ = 0;
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

    // The members, then the bases below - [class.dtor]/8's order. The member
    // half is shared with the destructor the program writes, which is where it
    // was missing.
    {
        std::vector<StmtPtr> mine = memberDestructors(cls, type, thisSlot, pos);
        for (std::size_t i = 0; i < mine.size(); i++)
            body.push_back(std::move(mine[i]));
    }

    for (std::size_t n = bs.size(); n-- > 0; ) {
        const Type *base = bs[n].type;
        // D1 destroys the virtual bases, after this body has run - the mirror
        // of C1 building them before it. See synthesizeCompleteDtor.
        if (bs[n].isVirtual) continue;
        const Signature *dtor = destructorOf(base);
        if (dtor == nullptr) continue;
        if (dtor->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be destroyed by the destructor "
                           "the compiler would write: the destructor of its "
                           "base '" + base->tag() + "' is " +
                           (dtor->access == Access::Private ? "private"
                                                            : "protected"));
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
        args.push_back(std::move(me));
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

    current_->functions.push_back(Function(cls + "::~" + localOf(cls),
                                           types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    current_->functions.back().setInline(true);
    // The Microsoft wrapper: `??1` above stops at this class's own part, and
    // `??_D` destroys it and then the virtual bases.
    if (target_.microsoftNames() && type->hasVirtualBase()) {
        frameSize_ = savedFrame;
        synthesizeCompleteDtor(type, vbaseDestructorSymbol(cls), symbol, true,
                               pos);
    }
    if (!target_.microsoftNames()) {
        std::string d2;
        itaniumDestructorName(cls, type, false, &d2);
        if (type->hasVirtualBase()) {
            current_->functions.back().setSymbol(d2);
            frameSize_ = savedFrame;
            synthesizeCompleteDtor(type, symbol, d2, true, pos);
        } else {
            current_->functions.back().setAlias(d2);
        }
    }
    frameSize_ = savedFrame;

    // A virtual one carries the deleting form into the vtable beside it, the
    // same as a written virtual destructor does.
    if (isVirtual) synthesizeDeleting(cls, type, Access::Public, pos);
}

// The copy assignment operator the class did not write. The trivial line is drawn
// where the others are and measured the same way with cl. A polymorphic class is
// non-trivial even though the body leaves the vptr alone: it writes into its own.
void Parser::declareImplicitCopyAssign(const std::string &tag, const Type *type,
                                       std::size_t pos) {
    if (overloadsOf(assignmentKey(tag)) != nullptr) return;
    // [class.copy]/23.
    if (moveConstructorOf(type) != nullptr) return;

    // **A const member has no assignment to give**, so the operator the compiler would
    // write is deleted rather than non-trivial and none is declared. Asked over every
    // member first: the search below stops at the first that gives it work.
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++)
        if (ms[i].type->isConst()) return;

    bool work = type->polymorphic();
    const std::vector<Type::BaseSpec> &bs = type->bases();
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
}

// The copy constructor the class did not write, on the same measured line: a class
// writing any constructor still gets one and only writing a copy takes it away. The
// implicit *move* needs all five of [class.copy]/9's absences, two of them vacuous.
void Parser::declareImplicitMoveCtor(const std::string &tag, const Type *type,
                                     std::size_t pos, bool userDeclared) {
    if (userDeclared) return;
    if (moveConstructorOf(type) != nullptr) return;

    bool work = type->polymorphic();
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
}

void Parser::declareImplicitCopyCtor(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (copyConstructorOf(type) != nullptr) return;
    // [class.copy]/7: a user-declared move constructor **deletes** the implicit copy constructor.
    if (moveConstructorOf(type) != nullptr) return;

    bool work = type->polymorphic();
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (copyConstructorOf(bs[i].type) != nullptr) work = true;
    const std::vector<Member> &ms = type->members();
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
}

// The body of a default constructor nobody wrote: the bases in the order they were
// written, then the vptrs, then the members with constructors of their own. Scalars
// are left alone, which is what [dcl.init] means by default-initialisation.
void Parser::synthesizeDefaultCtor(std::size_t which) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const int savedFrame = frameSize_;
    frameSize_ = 0;
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
    int flagSlot = -1;
    if (target_.microsoftNames() && type->hasVirtualBase()) {
        flagSlot = allocateFrameSlot(types_.intType());
        params.push_back(Param{ types_.intType(), flagSlot });
    }

    std::vector<StmtPtr> body;
    {
        std::vector<StmtPtr> guarded =
            guardedVirtualBaseInit(type, cls, thisSlot, flagSlot, pos, -1,
                                   false, nullptr);
        for (std::size_t i = 0; i < guarded.size(); i++)
            body.push_back(std::move(guarded[i]));
    }

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *base = bs[i].type;
        if (base->tag().empty()) continue;
        // **A virtual base belongs to C1, not to this body.** Two things go
        // wrong when it is built here.
        if (bs[i].isVirtual) continue;
        if (overloadsOf(constructorKey(base->tag())) == nullptr) continue;
        const Signature *ctor = defaultConstructorOf(base);
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
        applyDefaults(chosen, defaults, pos);
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
        if (one == nullptr && type->kind() != Kind::Union)
            one = constructMember(cls, type, ms[i], thisSlot, none, pos, true);
        if (one != nullptr) body.push_back(std::move(one));
    }

    current_->functions.push_back(Function(cls + "::" + cls, types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    current_->functions.back().setInline(true);
    // The same two names a written constructor is emitted under: C1 for a
    // complete object and C2 for a base subobject, the second a label in front
    // of the first. Microsoft has one name and wants no alias.
    if (!target_.microsoftNames()) {
        const Type *fnType = types_.functionType(types_.get(Kind::Void),
                                                 std::vector<const Type *>(), false);
        std::string c2, why;
        if (itaniumConstructorName(cls, type, fnType, false, &c2, &why)) {
            // **A virtual base splits this the same way it splits a written
            // constructor.** The walk above skipped them, so this body is C2;
            // C1 builds them and calls it.
            if (type->hasVirtualBase()) {
                current_->functions.back().setSymbol(c2);
                frameSize_ = savedFrame;
                vttSlot_ = savedVtt;
                synthesizeCompleteCtor(type, std::vector<const Type *>(),
                                       symbol, c2, true, pos);
                return;
            }
            current_->functions.back().setAlias(c2);
        }
    }
    frameSize_ = savedFrame;
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
    const char *kind = assigning ? "copy assignment"
                     : moving    ? "move constructor"
                                 : "copy constructor";

    const int savedFrame = frameSize_;
    frameSize_ = 0;
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
    {
        // **Copied out of the source's own virtual base**, which is what the
        // Itanium C1 does with the argument forwarded to it.
        std::vector<StmtPtr> guarded =
            guardedVirtualBaseInit(type, cls, thisSlot, flagSlot, pos,
                                   thatSlot, moving, nullptr);
        for (std::size_t i = 0; i < guarded.size(); i++)
            body.push_back(std::move(guarded[i]));
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
        if (assigning) cc = copyAssignOf(base);
        else {
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
        if (assigning) cc = copyAssignOf(memberClass(mt));
        else {
            if (moving) cc = moveConstructorOf(memberClass(mt));
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

        body.push_back(mt->isArray() ? eachElement(indexSlot, count, std::move(one))
                                     : std::move(one));
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

    current_->functions.push_back(Function(cls + "::" + (assigning ? "operator="
                                                                  : cls),
                                           returns, std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
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
                frameSize_ = savedFrame;
                vttSlot_ = savedVtt;
                synthesizeCompleteCtor(type, ps, symbol, c2, true, pos, 0,
                                       moving);
                return;
            }
            current_->functions.back().setAlias(c2);
        }
    }
    frameSize_ = savedFrame;
    vttSlot_ = savedVtt;
}

// **To a fixed point, because a body can be what first calls another.** Giving
// Owner its constructor is what calls Held's, and Held's may not have been
// wanted by anything the program wrote.
void Parser::defineImplicitFunctions() {
    for (bool again = true; again; ) {
        again = false;
        for (std::size_t i = 0; i < functions_.size(); i++) {
            if (!functions_[i].implicit || !functions_[i].used ||
                functions_[i].defined)
                continue;
            functions_[i].defined = true;
            if (!functions_[i].name.empty() && functions_[i].name[0] == '~')
                                                    synthesizeDestructor(i);
            else if (functions_[i].name == "operator=") synthesizeCopy(i, true);
            else if (functions_[i].params.empty())   synthesizeDefaultCtor(i);
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
                                 bool volatileWritten) {
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

    // **`static const int k = 5;` written in the class needs no definition**, measured
    // rather than assumed: cl emits no symbol for one and folds the value in wherever
    // it is read. Anything else with an initialiser here is refused.
    if (consume("=")) {
        if (!d.type->isConst() || !d.type->isInteger())
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
    refuseVolatileWithLinkage(volatileWritten, false, d.pos);
    s.symbol = staticMemberSymbol(cls, d.name, d.type, access, d.pos);
    owner->addStaticMember(s);
}

// `int Counter::total = 0;` at file scope - the definition the declaration inside the
// class asked for. An ordinary global the class gave its name to, so all this adds is
// finding which member it is and taking the symbol from it.
void Parser::defineStaticMember(Declared &d, Program &program) {
    const Type *owner = findTypedef(d.qualifier);
    if (owner == nullptr || !owner->isStructOrUnion())
        src_.fail(d.pos, "'" + d.qualifier + "' is not a class");
    const Type::StaticMember *s = owner->findStaticMember(d.name);
    if (s == nullptr)
        src_.fail(d.pos, "'" + d.qualifier + "' declares no static member '" +
                         d.name + "'");
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
            overloadsOf(constructorKey(cls->tag())) != nullptr) {
            const std::string full = d.qualifier + "::" + d.name;
            if (s->type->isArray())
                src_.fail(d.pos, "'" + full + "' is a static member array of '" +
                                 cls->tag() + "', which has a constructor - an "
                                 "array with static storage duration whose "
                                 "elements have a constructor is not "
                                 "supported yet");
            Declared m = d;
            m.name = full;
            m.type = s->type;
            const std::string helper = target_.microsoftNames()
                ? atexitHelperName(s->symbol + "@") : std::string();
            dynamicInitialise(m, s->symbol, helper, owner->isSpecialization());
            expect(";");
            // Not isConst whatever the member says: the constructor writes it.
            program.globals.push_back(Global{ full, s->symbol, s->type,
                                              std::vector<GlobalPiece>(), false,
                                              false, false });
            program.globals.back().isInline = owner->isSpecialization();
            return;
        }

    std::vector<GlobalPiece> pieces;
    bool hasInit = false, stored = false;
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
    }
    expect(";");

    program.globals.push_back(Global{ d.qualifier + "::" + d.name, s->symbol,
                                      s->type, std::move(pieces), hasInit, false,
                                      s->type->isConst() && !stored });
    // A template's static member is defined by every unit that uses it, and
    // the linker keeps one - a weak object, as its vtable is.
    program.globals.back().isInline = owner->isSpecialization();
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
    Var *v = Var::global(cls + "::" + s.name);
    v->setSymbol(s.symbol);
    ExprPtr n(v);
    n->setType(s.type);
    return n;
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
                           bool constThis, Access access, bool inUnion,
                           bool isVirtual, bool isStatic, bool isPure) {
    if (inUnion)
        src_.fail(d.pos, "a member function of a union is not supported yet");

    const Type *fn = d.type;
    checkOperatorDeclarable(d.name, fn->params(), true, d.pos);

    std::string key = cls + "::" + d.name;
    std::vector<std::size_t> &set = functionIndex_[key];

    const std::vector<const Type *> &params = fn->params();
    for (std::size_t k = 0; k < set.size(); k++) {
        const Signature &f = functions_[set[k]];
        if (f.constThis == constThis && sameParameters(f.params, params))
            src_.fail(d.pos, "'" + key + "' is declared twice");
    }

    if (inUnion && isVirtual)
        src_.fail(d.pos, "a union cannot have a virtual function");

    // **A static member takes no slot and overrides nothing.**
    if (isStatic) {
        const std::string sym = memberSymbol(cls, d.name, fn, access, false,
                                             d.pos, false, true);
        if (!pendingDefaults_.empty()) {
            defaultArgs_[sym] = pendingDefaults_;
            defaultArgNamespace_[sym] = namespaceStack_;
        }
        pendingDefaults_.clear();
        set.push_back(functions_.size());
        functions_.push_back(Signature{
            d.name, sym,
            fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
            cls, false, access, false });
        functions_.back().isStaticMember = true;
        functions_.back().isNoexcept = pendingNoexcept_;
        pendingNoexcept_ = false;
        return;
    }

    // **A slot is taken once and then kept**: an override replaces the entry the base
    // put there, matching on the signature minus the return type. **And finding that
    // slot is what makes this virtual**, keyword or not - so the search runs first.
    std::vector<VSlot> &slots = vtables_[cls];
    std::size_t slot = slots.size();
    for (std::size_t i = 0; i < slots.size(); i++) {
        if (!overrides(slots[i], d.name, params, constThis)) continue;
        checkOverrideReturn(slots[i], fn->returns(), cls, d.name, d.pos);
        slot = i;
        isVirtual = true;
        break;
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
                    if (overrides(it->second[i], d.name, params, constThis)) {
                        checkOverrideReturn(it->second[i], fn->returns(), cls, d.name, d.pos);
                        isVirtual = true;
                        break;
                    }
            }
        }

    const std::string symbol = memberSymbol(cls, d.name, fn, access, constThis,
                                            d.pos, isVirtual);
    if (!pendingDefaults_.empty()) defaultArgs_[symbol] = pendingDefaults_;
    if (!pendingDefaults_.empty())
        defaultArgNamespace_[symbol] = namespaceStack_;
    pendingDefaults_.clear();
    set.push_back(functions_.size());
    functions_.push_back(Signature{
        d.name, symbol,
        fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
        cls, constThis, access, isVirtual });
    functions_.back().isNoexcept = pendingNoexcept_;
    pendingNoexcept_ = false;

    if (!isVirtual) return;

    // **A pure virtual's slot holds the runtime's trap, not this function.**
    const std::string entry = isPure ? pureVirtualSymbol() : symbol;
    if (slot < slots.size()) {
        slots[slot].symbol = entry;
        slots[slot].pure = isPure;
        slots[slot].returns = fn->returns();
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
                 VSlot{ d.name, entry, params, constThis, isPure, fn->returns() });
}

// A member function's linkage name. Never plain, and never affected by
// `extern "C"`: a member cannot have C linkage, so the two ABIs are the only
// choice here.
std::string Parser::memberSymbol(const std::string &cls, const std::string &name,
                                 const Type *fn, Access access, bool constThis,
                                 std::size_t pos, bool isVirtual, bool isStatic) {
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
                                              shown, code, constThis, &out, &why)
                   : microsoftMemberName(cls, findTypedef(cls), name, shown, code,
                                         constThis, &out, &why))
            : (owner != nullptr
                   ? itaniumLocalMemberName(*owner, cls, findTypedef(cls), name,
                                            fn, constThis, &out, &why)
                   : itaniumMemberName(cls, findTypedef(cls), name, fn, constThis,
                                       &out, &why));
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
    if (cLinkage_ > 0) return name;
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
        (cLinkage_ > 0 || plain == "main" || namespaceStack_.empty())
            ? plain : namespacePrefix() + plain;
    const std::string &key = qualified;
    checkOperatorDeclarable(key, params, false, pos, internal);
    const bool cName = cLinkage_ > 0 || key == "main";
    std::vector<std::size_t> &set = functionIndex_[key];

    for (std::size_t k = 0; k < set.size(); k++) {
        Signature &f = functions_[set[k]];
        // A specialization sits in this list under the template's plain name
        // so that resolution can see it. It is not a declaration of that
        // name, so a function written with the same parameters is a new one.
        if (f.fromTemplate && instantiationKey_.empty()) continue;
        if (f.variadic != variadic || !sameParameters(f.params, params)) continue;

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
        if (defining) {
            if (f.defined) src_.fail(pos, "'" + key + "' is defined twice");
            f.defined = true;
        }
        pendingNoexcept_ = false;

        // **[dcl.fct.default]/4: a later declaration adds defaults, it does not discard them.**
        if (!pendingDefaults_.empty()) {
            // The scope this declaration was written in, for
            // [dcl.fct.default]/5.
            defaultArgNamespace_[f.symbol] = namespaceStack_;
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
        return;
    }

    // A new parameter list, so a new function - unless the name can only hold
    // one. Both halves of that are refused here rather than at the link, where
    // the report would be about a duplicate symbol in a file nobody wrote.
    if (!set.empty()) {
        const Signature &first = functions_[set[0]];
        if (cName || first.cLinkage)
            src_.fail(pos, "'" + key + "' cannot be overloaded - " +
                           (key == "main" ? std::string("'main' is one function")
                                           : std::string("a name with C linkage "
                                             "carries one symbol")));
    }

    set.push_back(functions_.size());
    functions_.push_back(Signature{ key,
                                    functionSymbol(key, returns, params, variadic,
                                                   internal, pos),
                                    returns, params, variadic, defining, pos,
                                    cName, std::string(), false,
                                    Access::Public });
    functions_.back().isNoexcept = pendingNoexcept_;
    pendingNoexcept_ = false;
    if (pendingExplicitConversion_ && isConversionName(instantiationName(name)))
        functions_.back().isExplicit = true;
    pendingExplicitConversion_ = false;
    if (!pendingDefaults_.empty()) {
        defaultArgs_[functions_.back().symbol] = pendingDefaults_;
        defaultArgNamespace_[functions_.back().symbol] = namespaceStack_;
    }
    pendingDefaults_.clear();
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
    if (const std::vector<std::size_t> *set = overloadsOf(key)) {
        for (std::size_t k = 0; k < set->size(); k++) {
            const Signature &f = functions_[(*set)[k]];
            if (f.variadic == variadic && sameParameters(f.params, params))
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
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("...")) return false;
    auto pk = packs_.find(peek().text);
    if (pk == packs_.end()) return false;

    const std::vector<const Type *> members = pk->second.types;
    const bool pattern = members.size() == 1 &&
                         members[0]->kind() == Kind::TemplateParam;
    at_ += 2;
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
        packs_[base] = pb;
    }
    pk->second.names = made;
    return true;
}

// To the ',' or ')' that ends a default argument, counting brackets so that a call or
// a subscript written inside one keeps its own commas. `<` is not counted: such a
// default is refused where it is read rather than mis-parsed here.
void Parser::skipDefaultArgument() {
    int depth = 0;
    for (;;) {
        const Token &t = peek();
        if (t.kind == TokenKind::End)
            src_.fail(t.pos, "this default argument never ends");
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) {
            if (depth == 0) return;         // the ')' closing the parameters
            depth--;
        } else if (t.is(",") && depth == 0) return;
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
    if (overloadsOf(constructorKey(plain->tag())) != nullptr) return;

    const std::vector<Member> &all = plain->members();
    for (std::size_t i = 0; i < all.size(); i++) {
        const bool needsOne = all[i].type->isConst() || all[i].type->isReference();
        if (!needsOne) continue;
        if (memberInit_.find(plain->tag() + "::" + all[i].name) != memberInit_.end())
            continue;
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
    for (std::size_t i = 0; i < ms.size(); i++) {
        if (already.find(ms[i].name) != already.end()) continue;
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
    const Type *outerClass = currentClass_;
    currentClass_ = type;
    struct RestoreClass {
        Parser *p; const Type *was;
        ~RestoreClass() { p->currentClass_ = was; }
    } restoreClass{ this, outerClass };
    at_ = it->second;

    // `= {}` or `= {0}`: the member's bytes zeroed by memset, an array or a
    // scalar alike, which is what [dcl.init.list]/3 comes to for either.
    if (peek().is("{")) {
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
    ExprPtr value = decay(assign());

    // **A class-typed member is *built* from its initialiser, not assigned
    // one.**
    StmtPtr made;
    if (memberClass(m.type) != nullptr && !m.type->isReference() &&
        overloadsOf(constructorKey(memberClass(m.type)->tag())) != nullptr) {
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
    if (m.type->isArray()) return eachElement(indexSlot, count, std::move(one));
    return one;
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

void Parser::parameterTypes(std::vector<const Type *> &params, bool &variadic) {
    expect("(");
    variadic = false;
    pendingDefaults_.clear();
    writtenParams_.clear();
    writtenFor_.clear();
    std::vector<const Type *> written;
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
        Declared pd = declarator(pt, true);
        if (mentionsDeduced(pd.type))
            src_.fail(pd.pos, "a parameter's type cannot be deduced - `auto` "
                              "there is C++14, and this compiler is C++11");
        const Type *declared = pd.type;
        if (pd.type->isArray()) pd.type = types_.pointerTo(pd.type->pointee());
        if (pd.type->isVoid())
            src_.fail(pd.pos, "'void' is only a parameter list on its own");
        params.push_back(types_.withoutConst(pd.type));
        written.push_back(parameterAsWritten(declared, params.back()));

        // `int b = 3`. The tokens are left where they are and their position
        // recorded; a call that omits the argument reads them again.
        pendingDefaults_.resize(params.size(), 0);
        if (consume("=")) {
            if (peek().is("{"))
                src_.fail(peek().pos, "a braced default argument is not "
                                      "supported yet - write the value");
            pendingDefaults_.back() = at_;
            skipDefaultArgument();
            if (at_ == pendingDefaults_.back())
                src_.fail(peek().pos, "this parameter says '=' and then gives "
                                      "no default");
        }
        closed = peek().pos;
        if (consume(")")) break;
        expect(",");
    }

    requireDefaultsAreASuffix(pendingDefaults_, closed);
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

