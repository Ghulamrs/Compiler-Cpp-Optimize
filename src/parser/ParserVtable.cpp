// The parser: the Itanium ABI's vtable group, thunks and VTT. A `_ZTV` holds
// the primary table, one per non-primary base at any depth and one per
// virtual base with a vptr.

// A class with virtual bases also gets construction vtables and a VTT its
// base-subobject constructor reads its vptrs from.

// Measured against clang's -fdump-vtable-layouts and its `_ZTT` and `_ZTC`
// symbols; TMS6747.md, "Virtual inheritance".

#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

// Where a virtual base sits, which only the class that laid it down knows.
int Parser::virtualBaseAt(const Type *layout, const Type *vbase) {
    const std::vector<Type::BaseSpec> &bs = layout->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (bs[i].isVirtual && bs[i].type == vbase) return bs[i].offset;
    return -1;
}

std::string Parser::itaniumClassEncoding(const Type *cls) const {
    return vtableSymbol(cls, false).substr(4);
}

// **The primary base's first, then this class's in inheritance-graph
// preorder** - clang's VCallAndVBaseOffsetBuilder, whose list is laid down
// reversed, so the last of these sits lowest in the table.
void Parser::vbaseComponents(const Type *cls, const Type *layout, int clsAt,
                             std::set<const Type *> &seen,
                             std::vector<std::pair<const Type *, int> > &out)
    const {
    if (const Type *p = cls->primaryBase())
        vbaseComponents(p, layout, clsAt, seen, out);
    const std::vector<Type::BaseSpec> &bs = cls->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (bs[i].isVirtual && seen.insert(bs[i].type).second)
            out.push_back(std::make_pair(bs[i].type,
                                         virtualBaseAt(layout, bs[i].type)));
}

// Where a virtual base's `vbase_offset` sits in this class's vtable, which is
// what the type_info names rather than the base's place in the object: the
// first component is nearest the address point, three entries below it.
long long Parser::itaniumVbaseOffsetSlot(const Type *cls, const Type *vbase) const {
    std::vector<std::pair<const Type *, int> > vb;
    std::set<const Type *> seen;
    vbaseComponents(cls->unqualified(), cls->unqualified(), 0, seen, vb);
    for (std::size_t i = 0; i < vb.size(); i++)
        if (vb[i].first == vbase->unqualified())
            return -static_cast<long long>(3 + i) * pointerBytes();
    return 0;
}

void Parser::subobjectsOf(const Type *x, int xAt, const Type *layout,
                          std::set<const Type *> &seen,
                          std::vector<Subobject> &out) const {
    out.push_back(Subobject{ x, xAt });
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        int at = xAt + bs[i]->offset;
        if (bs[i]->isVirtual) {
            if (!seen.insert(bs[i]->type).second) continue;
            at = virtualBaseAt(layout, bs[i]->type);
        }
        subobjectsOf(bs[i]->type, at, layout, seen, out);
    }
}

bool Parser::containsSubobject(const Type *y, int yAt, const Type *x, int xAt,
                               const Type *layout) const {
    std::vector<Subobject> all;
    std::set<const Type *> seen;
    subobjectsOf(y, yAt, layout, seen, all);
    for (std::size_t i = 0; i < all.size(); i++)
        if (all[i].cls == x && all[i].at == xAt) return true;
    return false;
}

// The slot in a class's own table holding a function the class itself
// declared - not one it inherited - matching a base's slot; null otherwise.
const Parser::VSlot *Parser::declaredSlot(const Type *cls, const VSlot &s) const {
    std::map<std::string, std::vector<VSlot> >::const_iterator it =
        vtables_.find(cls->tag());
    if (it == vtables_.end()) return nullptr;
    const bool dtor = s.name == "~" || s.name == "~$deleting";
    if (dtor) {
        if (destructorOf(cls) == nullptr) return nullptr;
    } else {
        const std::vector<std::size_t> *set = overloadsOf(cls->tag() + "::" + s.name);
        if (set == nullptr) return nullptr;
        bool own = false;
        for (std::size_t k = 0; k < set->size() && !own; k++) {
            const Signature &f = functions_[(*set)[k]];
            if (f.owner == cls->tag() && f.constThis == s.constThis &&
                f.isVirtual && sameParameters(f.params, s.params))
                own = true;
        }
        if (!own) return nullptr;
    }
    for (std::size_t i = 0; i < it->second.size(); i++) {
        const VSlot &c = it->second[i];
        if (dtor ? c.name == s.name
                 : overrides(c, s.name, s.params, s.constThis))
            return &c;
    }
    return nullptr;
}

// **[class.virtual]/2**: of the subobjects of `root` that contain the target
// and declare an override, the one no other contains. None means the
// target's own entry stands; two means the program is ill-formed.
Parser::Overrider Parser::finalOverrider(const Type *root, int rootAt,
                                         const VSlot &s, const Type *target,
                                         int targetAt, const Type *layout,
                                         std::size_t pos) {
    std::vector<Subobject> all;
    std::set<const Type *> seen;
    subobjectsOf(root, rootAt, layout, seen, all);

    struct Candidate { const Type *cls; int at; const VSlot *slot; };
    std::vector<Candidate> found;
    for (std::size_t i = 0; i < all.size(); i++) {
        if (all[i].cls == target && all[i].at == targetAt) continue;
        bool dup = false;
        for (std::size_t k = 0; k < found.size(); k++)
            if (found[k].cls == all[i].cls && found[k].at == all[i].at) dup = true;
        if (dup) continue;
        if (!containsSubobject(all[i].cls, all[i].at, target, targetAt, layout))
            continue;
        if (const VSlot *d = declaredSlot(all[i].cls, s))
            found.push_back(Candidate{ all[i].cls, all[i].at, d });
    }
    std::vector<Candidate> keep;
    for (std::size_t i = 0; i < found.size(); i++) {
        bool inside = false;
        for (std::size_t k = 0; k < found.size() && !inside; k++)
            if (k != i && containsSubobject(found[k].cls, found[k].at,
                                            found[i].cls, found[i].at, layout))
                inside = true;
        if (!inside) keep.push_back(found[i]);
    }
    if (keep.empty())
        return Overrider{ target, targetAt, s.symbol, s.pure };
    if (keep.size() > 1)
        src_.fail(pos, "'" + root->tag() + "' has more than one final overrider "
                       "for '" + target->tag() + "::" +
                       (s.name == "~$deleting" ? std::string("~") : s.name) +
                       "': '" + keep[0].cls->tag() + "' and '" +
                       keep[1].cls->tag() + "' each override it and neither "
                       "is derived from the other - [class.virtual]/2");
    return Overrider{ keep[0].cls, keep[0].at, keep[0].slot->symbol,
                      keep[0].slot->pure };
}

// **One `vcall_offset` per function of the virtual base**, in the order
// clang's AddVCallOffsets walks them: the primary chain's, then the class's
// own new ones, then those of its non-primary bases; a destructor counts once.
void Parser::vcallEntries(const Type *y, int yAt, const Type *vbase,
                          int vbaseAt, const Type *root, int rootAt,
                          const Type *layout, std::vector<VcallEntry> &out,
                          std::size_t pos) {
    std::size_t from = 0;
    if (const Type *p = y->primaryBase()) {
        vcallEntries(p, yAt, vbase, vbaseAt, root, rootAt, layout, out, pos);
        from = vtables_[p->tag()].size();
    }
    const std::vector<VSlot> &slots = vtables_[y->tag()];
    for (std::size_t i = from; i < slots.size(); i++) {
        if (slots[i].name == "~$deleting") continue;
        const Overrider ov = finalOverrider(root, rootAt, slots[i], y, yAt,
                                            layout, pos);
        out.push_back(VcallEntry{ y, slots[i].name, slots[i].params,
                                  slots[i].constThis,
                                  static_cast<long long>(ov.at - vbaseAt) });
    }
    const std::vector<const Type::BaseSpec *> bs = y->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        if (bs[i]->isVirtual || bs[i]->type == y->primaryBase()) continue;
        if (!bs[i]->type->hasVptr()) continue;
        vcallEntries(bs[i]->type, yAt + bs[i]->offset, vbase, vbaseAt, root,
                     rootAt, layout, out, pos);
    }
}

// **A virtual thunk**: `this` moves by a fixed amount to the virtual base
// whose table holds the vcall offset, then by what that offset says, and the
// real function is called - _ZTv0_n24_N1D1fEv, `n16` first where the step is -16.
std::string Parser::synthesizeVirtualThunk(const Type *type, const VSlot &slot,
                                           long long fixed, long long vcallBack,
                                           std::size_t pos) {
    std::string name = "_ZTv";
    name += fixed < 0 ? "n" + std::to_string(-fixed) : std::to_string(fixed);
    name += "_n" + std::to_string(vcallBack) + "_" + slot.symbol.substr(2);
    for (std::size_t i = 0; i < current_->functions.size(); i++)
        if (current_->functions[i].symbol() == name) return name;

    const Type *self = types_.pointerTo(type);
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    const Type *offType = ptrdiffType();
    const int savedFrame = frameSize_;
    frameSize_ = 0;

    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    std::vector<int> argSlots;
    for (std::size_t i = 0; i < slot.params.size(); i++)
        argSlots.push_back(allocateFrameSlot(slot.params[i]));
    for (std::size_t i = 0; i < slot.params.size(); i++)
        params.push_back(Param{ slot.params[i], argSlots[i] });

    // this = (char *)this + fixed, held in its own slot for the two reads
    const int held = allocateFrameSlot(chars);
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    ExprPtr asChars(new Cast(chars, std::move(me)));
    asChars->setType(chars);
    ExprPtr step(new Num(fixed));
    step->setType(offType);
    ExprPtr stepped(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
    stepped->setType(chars);
    ExprPtr keep(Var::local(".vt", held));
    keep->setType(chars);
    ExprPtr save(new Assign(std::move(keep), std::move(stepped)));
    save->setType(chars);
    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new ExprStmt(std::move(save))));

    // (T *)(held + *(ptrdiff_t *)(*(char **)held - vcallBack))
    ExprPtr obj(Var::local(".vt", held));
    obj->setType(chars);
    ExprPtr asTable(new Cast(types_.pointerTo(chars), std::move(obj)));
    asTable->setType(types_.pointerTo(chars));
    ExprPtr vptr(new Unary('*', std::move(asTable)));
    vptr->setType(chars);
    ExprPtr back(new Num(-vcallBack));
    back->setType(offType);
    ExprPtr at(new Binary(BinOp::Add, std::move(vptr), std::move(back)));
    at->setType(chars);
    ExprPtr asOff(new Cast(types_.pointerTo(offType), std::move(at)));
    asOff->setType(types_.pointerTo(offType));
    ExprPtr delta(new Unary('*', std::move(asOff)));
    delta->setType(offType);
    ExprPtr from(Var::local(".vt", held));
    from->setType(chars);
    ExprPtr moved(new Binary(BinOp::Add, std::move(from), std::move(delta)));
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

    const Type *returns = types_.get(Kind::Void);
    if (slot.name != "~" && slot.name != "~$deleting")
        if (const std::vector<std::size_t> *set =
                overloadsOf(type->tag() + "::" + slot.name))
            for (std::size_t k = 0; k < set->size(); k++)
                if (functions_[(*set)[k]].symbol == slot.symbol)
                    returns = functions_[(*set)[k]].returns;
    ExprPtr call = completeCall(slot.name, slot.symbol, nullptr, returns, full,
                                false, pos, std::move(args), true);
    if (returns->isVoid()) {
        body.push_back(StmtPtr(new ExprStmt(std::move(call))));
        body.push_back(StmtPtr(new Return(nullptr)));
    } else {
        body.push_back(StmtPtr(new Return(std::move(call))));
    }

    current_->functions.push_back(Function(name, returns, std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(name);
    current_->functions.back().setInline(true);
    frameSize_ = savedFrame;
    return name;
}

// **One table**, for the subobject `x` at `xAt` in the group of `root`: vcall
// offsets when x is itself a virtual base, its vbase offsets, offset-to-top,
// the type_info, then x's slots as `root` overrides them (`vbase` holds x).
void Parser::layoutOneVtable(VtableGroup &g, const Type *x, int xAt,
                             bool xIsVirtual, const Type *vbase, int vbaseAt,
                             const Type *root, int rootAt, const Type *layout,
                             const std::string &typeInfo, std::size_t pos) {
    const int w = pointerBytes();
    int at = g.pieces.empty() ? 0 : g.pieces.back().offset + g.pieces.back().size;
    const std::vector<VSlot> slots = vtables_[x->tag()];

    std::vector<Overrider> ov;
    for (std::size_t i = 0; i < slots.size(); i++)
        ov.push_back(finalOverrider(root, rootAt, slots[i], x, xAt, layout, pos));

    // The vcall offsets live in the virtual base's own table, x's when it
    // is one; a base inside it reaches them through that base's vptr.
    std::vector<VcallEntry> vcalls;
    if (vbase != nullptr)
        vcallEntries(vbase, vbaseAt, vbase, vbaseAt, root, rootAt, layout,
                     vcalls, pos);
    if (xIsVirtual)
        for (std::size_t k = vcalls.size(); k-- > 0; ) {
            g.pieces.push_back(GlobalPiece{ at, w, vcalls[k].value,
                                            std::string() });
            at += w;
        }

    std::vector<std::pair<const Type *, int> > vb;
    std::set<const Type *> seen;
    vbaseComponents(x, layout, xAt, seen, vb);
    for (std::size_t k = vb.size(); k-- > 0; ) {
        g.pieces.push_back(GlobalPiece{ at, w,
            static_cast<long long>(vb[k].second - xAt), std::string() });
        at += w;
    }
    g.pieces.push_back(GlobalPiece{ at, w,
        static_cast<long long>(rootAt - xAt), std::string() });
    at += w;
    g.pieces.push_back(GlobalPiece{ at, w, 0, typeInfo });
    at += w;
    g.points[std::make_pair(x, xAt)] = at;

    // How many entries below the address point the vcall offsets of the
    // virtual base's table begin, past its header and vbase offsets.
    std::vector<std::pair<const Type *, int> > vbOfBase;
    if (vbase != nullptr) {
        std::set<const Type *> seen2;
        vbaseComponents(vbase, layout, vbaseAt, seen2, vbOfBase);
    }
    for (std::size_t i = 0; i < slots.size(); i++) {
        std::string entry = ov[i].symbol;
        markSymbolUsed(entry);
        // The overrider's own slot, whose name and parameters the thunk copies.
        VSlot target = slots[i];
        target.symbol = ov[i].symbol;
        if (ov[i].at != xAt && !ov[i].pure) {
            const bool viaVbase = vbase != nullptr &&
                !containsSubobject(vbase, vbaseAt, ov[i].cls, ov[i].at, layout);
            if (viaVbase) {
                const std::string want = slots[i].name == "~$deleting"
                                       ? std::string("~") : slots[i].name;
                std::size_t idx = vcalls.size();
                for (std::size_t k = 0; k < vcalls.size() && idx == vcalls.size(); k++)
                    if (vcalls[k].cls == x && vcalls[k].name == want &&
                        vcalls[k].constThis == slots[i].constThis &&
                        sameParameters(vcalls[k].params, slots[i].params))
                        idx = k;
                if (idx == vcalls.size())
                    src_.fail(pos, "no vcall offset for '" + x->tag() + "::" +
                                   want + "' in '" + vbase->tag() + "'");
                const long long back =
                    static_cast<long long>(3 + vbOfBase.size() + idx) * w;
                entry = synthesizeVirtualThunk(ov[i].cls, target,
                                               static_cast<long long>(vbaseAt - xAt),
                                               back, pos);
            } else {
                entry = synthesizeThunk(ov[i].cls->tag(), ov[i].cls, target,
                                        xAt - ov[i].at, pos);
            }
        }
        g.pieces.push_back(GlobalPiece{ at, w, 0, entry });
        at += w;
    }
}

// **A secondary table for every non-primary base at any depth**: a primary
// base shares its parent's, and only its own bases are looked at. A
// construction group leaves out a base with no virtual base in or above it.
void Parser::layoutSecondaryVtables(VtableGroup &g, const Type *x, int xAt,
                                    const Type *vbase, int vbaseAt,
                                    const Type *root, int rootAt,
                                    const Type *layout, bool construction,
                                    const std::string &typeInfo,
                                    std::size_t pos) {
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *b = bs[i]->type;
        if (bs[i]->isVirtual || !b->hasVptr()) continue;
        if (construction && vbase == nullptr && !b->hasVirtualBase()) continue;
        const int bAt = xAt + bs[i]->offset;
        if (b != x->primaryBase())
            layoutOneVtable(g, b, bAt, false, vbase, vbaseAt, root, rootAt,
                            layout, typeInfo, pos);
        layoutSecondaryVtables(g, b, bAt, vbase, vbaseAt, root, rootAt, layout,
                               construction, typeInfo, pos);
    }
}

// **And one for every virtual base with a vptr**, met in inheritance-graph
// preorder and laid down once, with the secondary tables of its own
// non-primary bases behind it.
void Parser::layoutVirtualBaseVtables(VtableGroup &g, const Type *x,
                                      const Type *root, int rootAt,
                                      const Type *layout, bool construction,
                                      const std::string &typeInfo,
                                      std::set<const Type *> &seen,
                                      std::size_t pos) {
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *b = bs[i]->type;
        if (bs[i]->isVirtual && b->hasVptr() && seen.insert(b).second) {
            const int bAt = virtualBaseAt(layout, b);
            layoutOneVtable(g, b, bAt, true, b, bAt, root, rootAt, layout,
                            typeInfo, pos);
            layoutSecondaryVtables(g, b, bAt, b, bAt, root, rootAt, layout,
                                   construction, typeInfo, pos);
        }
        if (b->hasVirtualBase())
            layoutVirtualBaseVtables(g, b, root, rootAt, layout, construction,
                                     typeInfo, seen, pos);
    }
}

Parser::VtableGroup Parser::buildVtableGroup(const Type *cls, int clsAt,
                                             bool clsIsVirtual,
                                             const Type *layout,
                                             const std::string &typeInfo,
                                             std::size_t pos) {
    VtableGroup g;
    const bool construction = cls != layout;
    const Type *vbase = clsIsVirtual ? cls : nullptr;
    layoutOneVtable(g, cls, clsAt, clsIsVirtual, vbase, clsAt, cls, clsAt,
                    layout, typeInfo, pos);
    layoutSecondaryVtables(g, cls, clsAt, vbase, clsAt, cls, clsAt, layout,
                           construction, typeInfo, pos);
    std::set<const Type *> seen;
    layoutVirtualBaseVtables(g, cls, cls, clsAt, layout, construction,
                             typeInfo, seen, pos);
    return g;
}

void Parser::emitVtableGroup(const std::string &symbol, VtableGroup g) {
    if (groups_.count(symbol) != 0) return;
    const Type *entry = types_.pointerTo(types_.get(Kind::Void));
    const Type *table = types_.arrayOf(entry,
                                       static_cast<long long>(g.pieces.size()));
    current_->globals.push_back(Global{ symbol, symbol, table, g.pieces,
                                        true, false, true, std::string(),
                                        true });
    groups_[symbol] = std::move(g);
}

// **A construction vtable**, `_ZTC<layout><offset>_<base>`: the base's group
// as if it were the complete object, with the virtual bases where the
// layout class put them and the base's own type_info.
std::string Parser::constructionVtable(const Type *base, int at,
                                       bool baseIsVirtual, const Type *layout,
                                       const std::string &typeInfo,
                                       std::size_t pos) {
    (void)typeInfo;
    const std::string symbol = "_ZTC" + itaniumClassEncoding(layout) +
                               std::to_string(at) + "_" +
                               itaniumClassEncoding(base);
    if (groups_.count(symbol) != 0) return symbol;
    const std::string ti = emitClassTypeInfo(base, base->tag(), pos);
    emitVtableGroup(symbol, buildVtableGroup(base, at, baseIsVirtual, layout,
                                             ti, pos));
    return symbol;
}

// **One VTT part**: the primary virtual pointer, a sub-VTT for each direct
// non-virtual base with virtual bases, then the secondary virtual pointers;
// the complete object's part ends with the virtual VTTs - [2.6.2].
void Parser::vttPart(const Type *cls, int clsAt, const std::string &group,
                     const Type *layout, bool top, const std::string &typeInfo,
                     VttLayout &out, std::size_t pos) {
    const int w = pointerBytes();
    const std::size_t from = out.pieces.size();
    const VtableGroup &g = groups_[group];
    std::map<std::pair<const Type *, int>, int>::const_iterator own =
        g.points.find(std::make_pair(cls, clsAt));
    if (own == g.points.end())
        src_.fail(pos, "'" + group + "' has no table for '" + cls->tag() + "'");
    out.pieces.push_back(GlobalPiece{ static_cast<int>(from) * w, w,
                                      own->second, group });

    const std::vector<const Type::BaseSpec *> bs = cls->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        if (bs[i]->isVirtual || !bs[i]->type->hasVirtualBase()) continue;
        if (top) out.subVtt[bs[i]->type] = static_cast<int>(out.pieces.size());
        const int bAt = clsAt + bs[i]->offset;
        const std::string sub = constructionVtable(bs[i]->type, bAt, false,
                                                   layout, typeInfo, pos);
        vttPart(bs[i]->type, bAt, sub, layout, false, typeInfo, out, pos);
    }
    std::set<const Type *> seen;
    vttSecondaryPointers(cls, clsAt, false, group, layout, top, seen, out,
                         static_cast<int>(from), pos);
    if (top) {
        std::set<const Type *> seenV;
        vttVirtualParts(cls, layout, typeInfo, seenV, out, pos);
    }
}

// **A secondary virtual pointer for each base that has virtual bases or is
// reached through one, and is not a non-virtual primary base** - the vptrs
// a base-subobject constructor cannot set from the class's own group.
void Parser::vttSecondaryPointers(const Type *x, int xAt, bool morally,
                                  const std::string &group, const Type *layout,
                                  bool top, std::set<const Type *> &seen,
                                  VttLayout &out, int from, std::size_t pos) {
    const int w = pointerBytes();
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *b = bs[i]->type;
        int bAt = xAt + bs[i]->offset;
        bool morallyB = morally, nvPrimary = b == x->primaryBase();
        if (bs[i]->isVirtual) {
            if (!seen.insert(b).second) continue;
            bAt = virtualBaseAt(layout, b);
            morallyB = true;
            nvPrimary = false;
        }
        if (!nvPrimary && b->hasVptr() && (b->hasVirtualBase() || morallyB)) {
            const VtableGroup &g = groups_[group];
            std::map<std::pair<const Type *, int>, int>::const_iterator p =
                g.points.find(std::make_pair(b, bAt));
            if (p == g.points.end())
                src_.fail(pos, "'" + group + "' has no table for '" + b->tag() +
                               "' at " + std::to_string(bAt));
            if (top)
                out.secondary[std::make_pair(b, bAt)] =
                    static_cast<int>(out.pieces.size()) - from;
            out.pieces.push_back(GlobalPiece{
                static_cast<int>(out.pieces.size()) * w, w, p->second, group });
        }
        vttSecondaryPointers(b, bAt, morallyB, group, layout, top, seen, out,
                             from, pos);
    }
}

// **A virtual VTT for every virtual base that has virtual bases of its
// own**: its part, built on a construction vtable, for its C2 to read.
void Parser::vttVirtualParts(const Type *x, const Type *layout,
                             const std::string &typeInfo,
                             std::set<const Type *> &seen, VttLayout &out,
                             std::size_t pos) {
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *b = bs[i]->type;
        if (bs[i]->isVirtual && seen.insert(b).second && b->hasVirtualBase()) {
            const int bAt = virtualBaseAt(layout, b);
            out.virtualVtt[b] = static_cast<int>(out.pieces.size());
            const std::string sub = constructionVtable(b, bAt, true, layout,
                                                       typeInfo, pos);
            vttPart(b, bAt, sub, layout, false, typeInfo, out, pos);
        }
        if (b->hasVirtualBase())
            vttVirtualParts(b, layout, typeInfo, seen, out, pos);
    }
}

// The class's `_ZTV`, and with virtual bases its `_ZTT` and every
// construction vtable the VTT names.
void Parser::emitItaniumVtables(const Type *cls, const std::string &tag,
                                const std::string &symbol, std::size_t pos) {
    const std::string typeInfo = emitClassTypeInfo(cls, tag, pos);
    emitVtableGroup(symbol, buildVtableGroup(cls, 0, false, cls, typeInfo, pos));
    if (!cls->hasVirtualBase()) return;

    VttLayout vtt;
    vttPart(cls, 0, symbol, cls, true, typeInfo, vtt, pos);
    const std::string name = "_ZTT" + itaniumClassEncoding(cls);
    const Type *entry = types_.pointerTo(types_.get(Kind::Void));
    const Type *table = types_.arrayOf(entry,
                                       static_cast<long long>(vtt.pieces.size()));
    current_->globals.push_back(Global{ name, name, table, vtt.pieces, true,
                                        false, true, std::string(), true });
    vtts_[tag] = std::move(vtt);
}

// ----- what a constructor does with all that

ExprPtr Parser::vttEntry(ExprPtr vtt, int entry) {
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    const Type *word = types_.pointerTo(types_.get(Kind::Void));
    ExprPtr asChars(new Cast(chars, std::move(vtt)));
    asChars->setType(chars);
    ExprPtr step(new Num(static_cast<long long>(entry) * pointerBytes()));
    step->setType(types_.intType());
    ExprPtr at(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
    at->setType(chars);
    ExprPtr slot(new Cast(types_.pointerTo(word), std::move(at)));
    slot->setType(types_.pointerTo(word));
    ExprPtr value(new Unary('*', std::move(slot)));
    value->setType(word);
    return value;
}

// The sub-VTT for a direct non-virtual base, `vtt + n` in this function's own.
ExprPtr Parser::vttForBase(const Type *cls, const Type *base) {
    std::map<std::string, VttLayout>::const_iterator it = vtts_.find(cls->tag());
    if (vttSlot_ < 0 || it == vtts_.end() ||
        it->second.subVtt.count(base) == 0)
        src_.fail(0, "'" + cls->tag() + "' has no sub-VTT for its base '" +
                     base->tag() + "'");
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    ExprPtr vtt(Var::local(".vtt", vttSlot_));
    vtt->setType(vttType());
    ExprPtr asChars(new Cast(chars, std::move(vtt)));
    asChars->setType(chars);
    ExprPtr step(new Num(static_cast<long long>(
        it->second.subVtt.find(base)->second) * pointerBytes()));
    step->setType(types_.intType());
    ExprPtr at(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
    at->setType(chars);
    ExprPtr whole(new Cast(vttType(), std::move(at)));
    whole->setType(vttType());
    return whole;
}

// `_ZTT<cls> + n`, for the complete object's own calls: its C2 with 0, a
// virtual base's C2 with that base's virtual VTT.
ExprPtr Parser::vttOfClass(const Type *cls, int entry) {
    std::map<std::string, VttLayout>::const_iterator it = vtts_.find(cls->tag());
    if (it == vtts_.end())
        src_.fail(0, "'" + cls->tag() + "' has no VTT");
    const Type *word = types_.pointerTo(types_.get(Kind::Void));
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    ExprPtr table(Var::global("_ZTT" + itaniumClassEncoding(cls)));
    table->setType(types_.arrayOf(word,
        static_cast<long long>(it->second.pieces.size())));
    ExprPtr asChars(new Cast(chars, decay(std::move(table))));
    asChars->setType(chars);
    ExprPtr step(new Num(static_cast<long long>(entry) * pointerBytes()));
    step->setType(types_.intType());
    ExprPtr at(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
    at->setType(chars);
    ExprPtr whole(new Cast(vttType(), std::move(at)));
    whole->setType(vttType());
    return whole;
}

// `*(ptrdiff_t *)(*(char **)object + slot)`: the `vbase_offset` behind the
// object's vptr, which is why the vptr is stored before any use of this.
ExprPtr Parser::vbaseOffsetRead(ExprPtr objectChars, const Type *cls,
                                const Type *vbase) {
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    const Type *offType = ptrdiffType();
    ExprPtr asTable(new Cast(types_.pointerTo(chars), std::move(objectChars)));
    asTable->setType(types_.pointerTo(chars));
    ExprPtr vptr(new Unary('*', std::move(asTable)));
    vptr->setType(chars);
    ExprPtr backNum(new Num(itaniumVbaseOffsetSlot(cls, vbase)));
    backNum->setType(offType);
    ExprPtr at(new Binary(BinOp::Add, std::move(vptr), std::move(backNum)));
    at->setType(chars);
    ExprPtr asOff(new Cast(types_.pointerTo(offType), std::move(at)));
    asOff->setType(types_.pointerTo(offType));
    ExprPtr delta(new Unary('*', std::move(asOff)));
    delta->setType(offType);
    return delta;
}

// Every vptr in the object below the class's own, as clang's getVTablePointers
// walks them: a non-virtual primary base shares its parent's, a virtual base
// is met once, and each remembers the virtual base it sits inside.
void Parser::vptrSites(const Type *x, int xAt, bool morally, const Type *vbase,
                       int fromVBase, const Type *layout,
                       std::set<const Type *> &seen,
                       std::vector<VptrSite> &out) const {
    const std::vector<const Type::BaseSpec *> bs = x->directBases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *b = bs[i]->type;
        int bAt = xAt + bs[i]->offset, from = fromVBase + bs[i]->offset;
        bool morallyB = morally, nvPrimary = b == x->primaryBase();
        const Type *inside = vbase;
        if (bs[i]->isVirtual) {
            if (!seen.insert(b).second) continue;
            bAt = virtualBaseAt(layout, b);
            morallyB = true;
            nvPrimary = false;
            inside = b;
            from = 0;
        }
        if (!nvPrimary && b->hasVptr())
            out.push_back(VptrSite{ b, bAt, morallyB, inside, from });
        vptrSites(b, bAt, morallyB, inside, from, layout, seen, out);
    }
}

// **The stores themselves.** A C2 or D2 of a class with virtual bases takes its
// own vptr and every one below a virtual base from its VTT, those tables
// depending on the complete object; every other vptr is the class's own group's.
std::vector<StmtPtr> Parser::itaniumVptrStores(const std::string &cls,
                                               const Type *memberOf,
                                               int thisSlot) {
    std::vector<StmtPtr> out;
    const Type *plain = memberOf->unqualified();
    const std::string table = plain->tag() == cls ? vtableSymbol(plain, false)
                                                   : vtableSymbol(cls, false);
    std::map<std::string, VtableGroup>::const_iterator git = groups_.find(table);
    if (git == groups_.end()) return out;
    const VtableGroup &g = git->second;
    const bool useVtt = takesVtt(plain) && vttSlot_ >= 0;
    const VttLayout *vtt = useVtt ? &vtts_[cls] : nullptr;

    const Type *word = types_.pointerTo(types_.get(Kind::Void));
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    const Type *offType = ptrdiffType();

    std::vector<VptrSite> sites;
    sites.push_back(VptrSite{ plain, 0, false, nullptr, 0 });
    std::set<const Type *> seen;
    vptrSites(plain, 0, false, nullptr, 0, plain, seen, sites);

    for (std::size_t i = 0; i < sites.size(); i++) {
        const VptrSite &s = sites[i];
        const std::pair<const Type *, int> key(s.cls, s.offset);

        // **The address point**, out of the VTT or out of the group.
        ExprPtr value;
        const bool fromVtt = useVtt && (i == 0 || s.cls->hasVirtualBase() ||
                                        s.morallyVirtual);
        if (fromVtt) {
            int entry = 0;
            if (i != 0) {
                std::map<std::pair<const Type *, int>, int>::const_iterator e =
                    vtt->secondary.find(key);
                if (e == vtt->secondary.end()) continue;
                entry = e->second;
            }
            ExprPtr v(Var::local(".vtt", vttSlot_));
            v->setType(vttType());
            value = vttEntry(std::move(v), entry);
        } else {
            std::map<std::pair<const Type *, int>, int>::const_iterator p =
                g.points.find(key);
            if (p == g.points.end()) continue;
            ExprPtr base(Var::global(table));
            base->setType(types_.arrayOf(word,
                static_cast<long long>(g.pieces.size())));
            ExprPtr asChars(new Cast(chars, decay(std::move(base))));
            asChars->setType(chars);
            ExprPtr skip(new Num(static_cast<long long>(p->second)));
            skip->setType(types_.intType());
            ExprPtr past(new Binary(BinOp::Add, std::move(asChars),
                                    std::move(skip)));
            past->setType(chars);
            value = ExprPtr(new Cast(word, std::move(past)));
            value->setType(word);
        }

        // **Where the vptr is**: a constant from `this`, or through the
        // virtual base's offset read from the vptr just stored.
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(chars);
        ExprPtr where = std::move(me);
        if (s.morallyVirtual) {
            ExprPtr again(Var::local("this", thisSlot));
            again->setType(chars);
            ExprPtr delta = vbaseOffsetRead(std::move(again), plain,
                                            s.nearestVBase);
            ExprPtr moved(new Binary(BinOp::Add, std::move(where),
                                     std::move(delta)));
            moved->setType(chars);
            where = std::move(moved);
            if (s.fromVBase != 0) {
                ExprPtr more(new Num(static_cast<long long>(s.fromVBase)));
                more->setType(offType);
                ExprPtr moved2(new Binary(BinOp::Add, std::move(where),
                                          std::move(more)));
                moved2->setType(chars);
                where = std::move(moved2);
            }
        } else if (s.offset != 0) {
            ExprPtr step(new Num(static_cast<long long>(s.offset)));
            step->setType(offType);
            ExprPtr moved(new Binary(BinOp::Add, std::move(where),
                                     std::move(step)));
            moved->setType(chars);
            where = std::move(moved);
        }
        ExprPtr slot(new Cast(types_.pointerTo(word), std::move(where)));
        slot->setType(types_.pointerTo(word));
        ExprPtr there(new Unary('*', std::move(slot)));
        there->setType(word);
        ExprPtr store(new Assign(std::move(there), std::move(value)));
        store->setType(word);
        out.push_back(StmtPtr(new ExprStmt(std::move(store))));
    }
    return out;
}
