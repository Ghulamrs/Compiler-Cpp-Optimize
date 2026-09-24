#include "OptPasses.h"

namespace opt {

namespace {

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }
bool is(const std::string &m, std::initializer_list<const char *> names) {
    for (const char *n : names) if (m == n) return true;
    return false;
}

// The bytes a load fetches, and whether what it leaves may stand for them at four bytes.
int loadWidth(const Instr &l) {
    if (!l.a.isMem() || !gpr(l.b)) return 0;
    if (l.m == "movl" || l.m == "movslq") return 4;
    if (isMovQ(l.m) && l.b.reg.width == 8) return 8;
    return 0;
}

// **Whether an instruction uses register r's value other than as an address**:
// as a source, as a destination it also reads, or implicitly.
bool readsAsValue(const Instr &i, const Effects &e, int r) {
    if ((i.a.kind == Operand::Register || i.a.kind == Operand::Indirect) && i.a.reg.id == r) return true;
    if ((i.a.indexed() && i.a.index.id == r) || (i.b.indexed() && i.b.index.id == r)) return true;
    const bool overwrites = i.m.compare(0, 3, "mov") == 0 || i.m.compare(0, 3, "set") == 0 || i.m == "lea";
    if (i.b.isReg(r) && (!overwrites || i.b.reg.width < 4)) return true;
    return !explicitOnly(i) && (e.reads & bit(r));
}

}

bool foldLoads(Stream &s, Flow &f, const Convention &c) {
    f.live(s);
    bool changed = false;
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        const Block &blk = f.blocks[b];
        Live live = blk.out;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            Entry &use = s[k];
            if (use.kind != Entry::Ins || use.dead) continue;
            f.joinPads(b, k, live);
            Instr &u = use.ins;
            const bool arith = is(u.m, {"add", "sub", "and", "or", "xor", "cmp", "imul"}) && u.operands == 2;
            // The operand a load could stand in for: the source, or cmp's other side.
            Operand *slot = nullptr;
            if (arith && gpr(u.a) && gpr(u.b) && u.a.reg.id != u.b.reg.id) slot = &u.a;
            else if (u.m == "cmp" && gpr(u.b) && (gpr(u.a) || u.a.kind == Operand::Immediate) &&
                     !(gpr(u.a) && u.a.reg.id == u.b.reg.id)) slot = &u.b;
            const int r = slot ? slot->reg.id : -1;
            if (slot && !(live.regs & bit(r))) {
                int l = k - 1;
                RegSet written = 0;
                bool clear = true;
                for (int n = 0; l >= blk.begin && n < 16; --l, ++n) {
                    if (s[l].kind != Entry::Ins || s[l].dead) continue;
                    const Effects &e = f.effects[l];
                    if (e.writes & bit(r)) break;
                    if ((e.reads & bit(r)) || e.memoryWritten || e.control || e.opaque || e.stack) { clear = false; break; }
                    written |= e.writes | e.partial;
                }
                const int w = loadWidth(l >= blk.begin ? s[l].ins : Instr());
                const bool fits = w != 0 && s[l].ins.b.reg.id == r && (w == slot->reg.width ||
                                  (w == 4 && slot->reg.width == 4));
                const int base = fits ? s[l].ins.a.reg.id : -1;
                const int index = fits && s[l].ins.a.indexed() ? s[l].ins.a.index.id : -1;
                if (clear && fits && s[l].kind == Entry::Ins && !s[l].dead &&
                    (base < 0 || !(written & bit(base))) && (index < 0 || !(written & bit(index)))) {
                    const int width = slot->reg.width;
                    *slot = s[l].ins.a;
                    if (slot == &u.b && u.a.kind == Operand::Immediate) u.m = width == 4 ? "cmpl" : "cmpq";
                    s[l].dead = true;
                    f.effects[k] = effectsOf(u, c);
                    changed = true;
                }
            }
            live.step(f.effects[k]);
        }
    }
    return changed;
}

bool foldOffsets(Stream &s, Flow &f, const Convention &c) {
    f.live(s);
    bool changed = false;
    for (const Block &blk : f.blocks) {
        for (int k = blk.begin; k < blk.end; ++k) {
            if (s[k].kind != Entry::Ins || s[k].dead) continue;
            const Instr &add = s[k].ins;
            if (!(add.m == "add" || add.m == "addq") || !gpr(add.b) || add.b.reg.width != 8 || frameReg(add.b.reg.id) ||
                add.a.kind != Operand::Immediate || !add.a.numeric)
                continue;
            const int r = add.b.reg.id;
            // **Every later use an address, until r is written again or dies**,
            // and nothing reads the flags the add set.
            std::vector<Operand *> uses;
            bool ok = true, ended = false, flagsSettled = false;
            for (int j = k + 1; j < blk.end && ok && !ended; ++j) {
                if (s[j].kind != Entry::Ins || s[j].dead) continue;
                Instr &u = s[j].ins;
                const Effects &e = f.effects[j];
                if (e.flagsRead && !flagsSettled) ok = false;
                if (e.flagsWritten) flagsSettled = true;
                if (readsAsValue(u, e, r) || e.control || e.opaque) { ok = false; break; }
                for (Operand *o : {&u.a, &u.b})
                    if (o->kind == Operand::Memory && o->reg.id == r) uses.push_back(o);
                if (e.writes & bit(r)) ended = true;
            }
            if (!ended && (blk.out.regs & bit(r))) ok = false;
            if (!ended && blk.out.flags && !flagsSettled) ok = false;
            if (!ok || uses.empty()) continue;
            for (Operand *o : uses) {
                if (!(o->disp + add.a.value == static_cast<int>(o->disp + add.a.value))) { ok = false; break; }
            }
            if (!ok) continue;
            for (Operand *o : uses) { o->disp += add.a.value; o->hasDisp = true; }
            for (int j = k + 1; j < blk.end; ++j)
                if (s[j].kind == Entry::Ins && !s[j].dead) f.effects[j] = effectsOf(s[j].ins, c);
            s[k].dead = true;
            changed = true;
        }
    }
    return changed;
}

namespace {

bool reg64(const Operand &o) { return gpr(o) && o.reg.width == 8 && !frameReg(o.reg.id); }
bool jump(const Instr &i) { const Opcode::Kind k = opcodeOf(i.m).kind; return k == Opcode::Jmp || k == Opcode::Jcc; }

// **`add %idx, %base` where base is then only an address** - every use a
// memory base until it is written again or dies, idx unchanged meanwhile, the
// flags unread - becomes `(%base,%idx,1)` at each use, and the add goes.
bool foldAdd(Stream &s, Flow &f, const Convention &c, const Block &blk, int k) {
    const Instr &add = s[k].ins;
    if (add.m != "add" || add.operands != 2 || !reg64(add.a) || !reg64(add.b) || add.a.reg.id == add.b.reg.id) return false;
    const int idx = add.a.reg.id, base = add.b.reg.id;
    std::vector<Operand *> uses;
    bool ok = true, ended = false, flagsSettled = false, idxGone = false;
    for (int j = k + 1; j < blk.end && ok; ++j) {
        if (s[j].kind != Entry::Ins || s[j].dead) continue;
        Instr &u = s[j].ins;
        const Effects &e = f.effects[j];
        if (e.flagsRead && !flagsSettled) ok = false;
        if (e.flagsWritten) flagsSettled = true;
        if (ended) continue;
        if (readsAsValue(u, e, base) || (e.control && !jump(u)) || e.opaque) { ok = false; break; }
        for (Operand *o : {&u.a, &u.b})
            if (o->kind == Operand::Memory && o->reg.id == base) { if (o->scale != 0 || idxGone) ok = false; else uses.push_back(o); }
        if (e.writes & bit(base)) ended = true;
        else if ((e.writes | e.partial) & bit(idx)) idxGone = true;
    }
    if (!ended && (blk.out.regs & bit(base))) ok = false;
    if (!ended && blk.out.flags && !flagsSettled) ok = false;
    if (!ok || uses.empty()) return false;
    for (Operand *o : uses) { o->index = Reg{idx, 8}; o->scale = 1; }
    s[k].dead = true;
    for (int j = k + 1; j < blk.end; ++j)
        if (s[j].kind == Entry::Ins && !s[j].dead) f.effects[j] = effectsOf(s[j].ins, c);
    return true;
}

// The scale a `shl $k` or an `imul $s` of an index register stands for; 0 for neither.
int scaleOf(const Instr &i) {
    if (i.operands != 2 || i.a.kind != Operand::Immediate || !i.a.numeric || !reg64(i.b)) return 0;
    if (i.m == "shl" && i.a.value >= 1 && i.a.value <= 3) return 1 << i.a.value;
    if (i.m == "imul" && (i.a.value == 2 || i.a.value == 4 || i.a.value == 8)) return static_cast<int>(i.a.value);
    return 0;
}

// **An index scaled by a shift or a multiply, then only ever an index at scale
// one**, until written again or dead, carries the scale in the operand instead.
bool foldScale(Stream &s, Flow &f, const Convention &c, const Block &blk, int k) {
    const int scale = scaleOf(s[k].ins);
    if (scale == 0) return false;
    const int r = s[k].ins.b.reg.id;
    std::vector<Operand *> uses;
    bool ok = true, ended = false, flagsSettled = false;
    for (int j = k + 1; j < blk.end && ok; ++j) {
        if (s[j].kind != Entry::Ins || s[j].dead) continue;
        Instr &u = s[j].ins;
        const Effects &e = f.effects[j];
        if (e.flagsRead && !flagsSettled) ok = false;
        if (e.flagsWritten) flagsSettled = true;
        if (ended) continue;
        const Roles roles = rolesOf(u);
        for (int n = 0; n < 2; ++n) {
            Operand *o = n == 0 ? &u.a : &u.b;
            const unsigned role = n == 0 ? roles.a : roles.b;
            if (o->indexed() && o->index.id == r && o->scale == 1 && o->reg.id != r) { uses.push_back(o); continue; }
            if (o->indexed() && o->index.id == r) ok = false;
            if (o->kind != Operand::Register && o->kind != Operand::Memory && o->kind != Operand::Indirect) continue;
            if (o->reg.id != r) continue;
            const bool overwrite = o->kind == Operand::Register && role == kWrite && o->reg.width >= 4;
            if (!overwrite) ok = false;
        }
        if (!explicitOnly(u) && (e.reads & bit(r))) ok = false;
        if (e.opaque) ok = false;
        if ((e.writes | e.partial) & bit(r)) ended = true;
    }
    if (!ended && (blk.out.regs & bit(r))) ok = false;
    if (!ended && blk.out.flags && !flagsSettled) ok = false;
    if (!ok || uses.empty()) return false;
    for (Operand *o : uses) o->scale = scale;
    s[k].dead = true;
    for (int j = k + 1; j < blk.end; ++j)
        if (s[j].kind == Entry::Ins && !s[j].dead) f.effects[j] = effectsOf(s[j].ins, c);
    return true;
}

}

bool foldIndex(Stream &s, Flow &f, const Convention &c) {
    f.live(s);
    bool changed = false;
    for (const Block &blk : f.blocks) {
        for (int k = blk.begin; k < blk.end; ++k)
            if (s[k].kind == Entry::Ins && !s[k].dead && foldAdd(s, f, c, blk, k)) changed = true;
        for (int k = blk.begin; k < blk.end; ++k)
            if (s[k].kind == Entry::Ins && !s[k].dead && foldScale(s, f, c, blk, k)) changed = true;
    }
    return changed;
}

}
