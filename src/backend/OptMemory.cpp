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
    const bool overwrites = i.m.compare(0, 3, "mov") == 0 || i.m.compare(0, 3, "set") == 0 || i.m == "lea";
    if (i.b.isReg(r) && (!overwrites || i.b.reg.width < 4)) return true;
    return !explicitOnly(i) && (e.reads & bit(r));
}

}

bool foldLoads(Stream &s, Flow &f, const Convention &c) {
    f.live(s);
    bool changed = false;
    for (const Block &blk : f.blocks) {
        RegSet live = blk.liveOut;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            Entry &use = s[k];
            if (use.kind != Entry::Ins || use.dead) continue;
            Instr &u = use.ins;
            const bool arith = is(u.m, {"add", "sub", "and", "or", "xor", "cmp", "imul"}) && u.operands == 2;
            // The operand a load could stand in for: the source, or cmp's other side.
            Operand *slot = nullptr;
            if (arith && gpr(u.a) && gpr(u.b) && u.a.reg.id != u.b.reg.id) slot = &u.a;
            else if (u.m == "cmp" && gpr(u.b) && (gpr(u.a) || u.a.kind == Operand::Immediate) &&
                     !(gpr(u.a) && u.a.reg.id == u.b.reg.id)) slot = &u.b;
            const int r = slot ? slot->reg.id : -1;
            if (slot && !(live & bit(r))) {
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
                if (clear && fits && s[l].kind == Entry::Ins && !s[l].dead &&
                    (base < 0 || !(written & bit(base)))) {
                    const int width = slot->reg.width;
                    *slot = s[l].ins.a;
                    if (slot == &u.b && u.a.kind == Operand::Immediate) u.m = width == 4 ? "cmpl" : "cmpq";
                    s[l].dead = true;
                    f.effects[k] = effectsOf(u, c);
                    changed = true;
                }
            }
            const Effects &e = f.effects[k];
            live = (live & ~e.writes) | e.reads;
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
            if (!ended && (blk.liveOut & bit(r))) ok = false;
            if (!ended && blk.flagsOut && !flagsSettled) ok = false;
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

}
