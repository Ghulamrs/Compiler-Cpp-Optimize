#include "OptPasses.h"

namespace opt {

namespace {

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }

// `movslq %eax, %rax` and `mov %eax, %eax`: the low half stays as it was.
bool extendsInPlace(const Instr &i) {
    if (!gpr(i.a) || !gpr(i.b) || i.a.reg.id != i.b.reg.id || i.a.reg.width != 4) return false;
    return (i.m == "movslq" && i.b.reg.width == 8) || (isMovQL(i.m) && i.b.reg.width == 4);
}

}

// rsp and rbp are the frame; nothing that writes them is ever taken for dead.
bool removeDead(Stream &s, Flow &f) {
    return removeDeadIn(s, f, bit(RSP) | bit(RBP), [](const Instr &i, RegSet wide) {
        return extendsInPlace(i) && (wide & bit(i.b.reg.id)) == 0;
    });
}

bool removeUnreachable(Stream &s) { return removeUnreachableIn(s); }

namespace {

// **A value taken out of X, worked on in t and put back** is worked on in X:
// `mov %X,%t ... mov %t,%X` with X untouched between and t dead after.
bool workInPlace(Stream &s, Flow &f, const Convention &conv, int k, int begin, const Live &live) {
    const Instr &c = s[k].ins;
    if (!(c.m == "mov" || c.m == "movl" || c.m == "movq") || !gpr(c.a) || !gpr(c.b)) return false;
    const int t = c.a.reg.id, x = c.b.reg.id, w = c.b.reg.width;
    if (t == x || frameReg(t) || frameReg(x) || c.a.reg.width != w || w < 4 || (live.regs & bit(t)) || (w == 4 && (live.wide & bit(x)))) return false;
    for (int p = k - 1, n = 0; p >= begin && n < 32; --p, ++n) {
        if (s[p].kind != Entry::Ins || s[p].dead) continue;
        const Instr &i = s[p].ins;
        const Effects &e = f.effects[p];
        const bool copyIn = gpr(i.a) && i.a.reg.id == x && gpr(i.b) && i.b.reg.id == t && i.b.reg.width >= 4 &&
                            (i.m == "mov" || i.m == "movl" || i.m == "movq" || i.m == "movslq");
        // A plain four-byte copy-in zeroed the upper half the renamed value keeps.
        if (copyIn && i.m != "movslq" && i.b.reg.width < w) return false;
        if (copyIn) {
            for (int q = p; q < k; ++q) {
                if (s[q].kind != Entry::Ins || s[q].dead) continue;
                for (Operand *o : {&s[q].ins.a, &s[q].ins.b}) {
                    if ((o->kind == Operand::Register || o->kind == Operand::Memory) && o->reg.id == t) o->reg.id = x;
                    if (o->indexed() && o->index.id == t) o->index.id = x;
                }
                f.effects[q] = effectsOf(s[q].ins, conv);
            }
            s[k].dead = true;
            if (s[p].ins.m != "movslq" && s[p].ins.a.reg.width == s[p].ins.b.reg.width) s[p].dead = true;
            return true;
        }
        const RegSet touched = e.reads | e.writes | e.partial;
        if ((touched & bit(x)) || e.control || e.opaque || e.stack) return false;
        if ((touched & bit(t)) && !explicitOnly(i)) return false;
    }
    return false;
}

bool xmm(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= kXmm0 && o.reg.id < kXmm0 + 16; }
bool xmmCopy(const Instr &i) { return i.m == "movapd" && xmm(i.a) && xmm(i.b) && i.a.reg.id != i.b.reg.id; }
bool sseArith(const std::string &m) { return m == "addsd" || m == "subsd" || m == "mulsd" || m == "divsd"; }
int nextIns(const Stream &s, int k, int end) {
    for (++k; k < end; ++k) if (s[k].kind == Entry::Ins && !s[k].dead) return k;
    return -1;
}

// **The SSE stack discipline leaves copies a GPR value never gets**: a load
// copied on, a copy fed to one arithmetic, and a copy kept while its source
// is reloaded and added back. Each is folded when the copy's target is dead.
bool coalesceXmm(Stream &s, Flow &f, const Convention &conv) {
    f.live(s);
    bool changed = false;
    std::vector<Live> after(s.size());
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        const Block &blk = f.blocks[b];
        Live live = blk.out;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            if (s[k].kind != Entry::Ins || s[k].dead) continue;
            f.joinPads(b, k, live);
            after[k] = live;
            live.step(f.effects[k]);
        }
        for (int k = blk.begin; k < blk.end; ++k) {
            if (s[k].kind != Entry::Ins || s[k].dead || !xmmCopy(s[k].ins)) continue;
            const int src = s[k].ins.a.reg.id, dst = s[k].ins.b.reg.id;
            const int u = nextIns(s, k, blk.end);
            if (u < 0) continue;
            Instr &n = s[u].ins;
            // `movapd %s,%d; op %d,%x` with d dead after: `op %s,%x`.
            if (sseArith(n.m) && xmm(n.a) && n.a.reg.id == dst && xmm(n.b) && n.b.reg.id != dst &&
                !(after[u].regs & bit(dst))) {
                n.a.reg.id = src;
                f.effects[u] = effectsOf(n, conv);
                s[k].dead = changed = true;
                continue;
            }
            // `movapd %s,%d; movsd mem,%s; addsd %d,%s` with d dead: `addsd mem,%s`. A
            // frame slot is left alone, or the locals pass could not promote it.
            const int v = nextIns(s, u, blk.end);
            if (v >= 0 && n.m == "movsd" && n.a.isMem() && xmm(n.b) && n.b.reg.id == src &&
                !frameReg(n.a.reg.id) && (s[v].ins.m == "addsd" || s[v].ins.m == "mulsd") &&
                xmm(s[v].ins.a) && s[v].ins.a.reg.id == dst && xmm(s[v].ins.b) && s[v].ins.b.reg.id == src &&
                !(after[v].regs & bit(dst))) {
                s[v].ins.a = n.a;
                f.effects[v] = effectsOf(s[v].ins, conv);
                s[k].dead = s[u].dead = changed = true;
            }
        }
    }
    return changed;
}

}

bool coalesceCopies(Stream &s, Flow &f, const Convention &conv) {
    if (coalesceXmm(s, f, conv)) return true;
    f.live(s);
    bool changed = false;
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        const Block &blk = f.blocks[b];
        Live live = blk.out;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            Entry &copy = s[k];
            if (copy.kind != Entry::Ins || copy.dead) continue;
            f.joinPads(b, k, live);
            const Instr &c = copy.ins;
            const bool isCopy = ((c.m == "mov" || c.m == "movq") && gpr(c.a) && gpr(c.b) &&
                                !frameReg(c.a.reg.id) && !frameReg(c.b.reg.id) &&
                                c.a.reg.width == 8 && c.b.reg.width == 8 && c.a.reg.id != c.b.reg.id) || xmmCopy(c);
            int p = k - 1;
            while (p >= blk.begin && (s[p].kind == Entry::Event || s[p].dead)) --p;
            if (isCopy && p >= blk.begin && s[p].kind == Entry::Ins && !(live.regs & bit(c.a.reg.id))) {
                Instr &w = s[p].ins;
                const Effects &we = f.effects[p];
                const RegSet r = bit(c.a.reg.id);
                const bool pure = (gpr(w.b) || xmm(w.b)) && w.b.reg.id == c.a.reg.id && w.b.reg.width >= 4 &&
                                  (we.writes & r) && !(we.reads & r) && !(we.partial & r) &&
                                  explicitOnly(w) && w.operands == 2 && !we.flagsWritten;
                if (pure) {
                    w.b.reg.id = c.b.reg.id;
                    f.effects[p].writes = (we.writes & ~r) | bit(c.b.reg.id);
                    copy.dead = changed = true;
                    continue;
                }
            }
            if (workInPlace(s, f, conv, k, blk.begin, live)) { changed = true; break; }
            live.step(f.effects[k]);
        }
    }
    return changed;
}

}
