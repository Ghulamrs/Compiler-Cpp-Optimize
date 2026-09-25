#include "OptPasses.h"

#include <algorithm>

namespace opt {

namespace {

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }

// **Hacker's Delight's magic number for a signed 32-bit divisor** d >= 2:
// q = (n * M) >> (32 + s), M taken unsigned in a 64-bit product, plus one
// where n is negative.
void signedMagic(unsigned d, unsigned long long &M, int &s) {
    const unsigned long long two31 = 1ULL << 31;
    const unsigned long long anc = two31 - 1 - two31 % d;
    int p = 31;
    unsigned long long q1 = two31 / anc, r1 = two31 - q1 * anc;
    unsigned long long q2 = two31 / d, r2 = two31 - q2 * d;
    unsigned long long delta;
    do {
        ++p;
        q1 *= 2; r1 *= 2;
        if (r1 >= anc) { ++q1; r1 -= anc; }
        q2 *= 2; r2 *= 2;
        if (r2 >= d) { ++q2; r2 -= d; }
        delta = d - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));
    M = (q2 + 1) & 0xffffffffULL;
    s = p - 32;
}

// An unsigned 32-bit divisor's multiplier where one below 2^32 is exact:
// M = ceil(2^(32+s) / d) with M*d - 2^(32+s) < 2^s; false where none is.
bool unsignedMagic(unsigned d, unsigned long long &M, int &s) {
    for (s = 0; s < 32; ++s) {
        const unsigned long long p = 1ULL << (32 + s);
        M = (p + d - 1) / d;
        if (M >= (1ULL << 32)) return false;
        if (M * d - p < (1ULL << s)) return true;
    }
    return false;
}

Instr two(const char *m, Operand a, Operand b) { return Instr{m, a, b, 2}; }
Operand r32(int id) { return Operand::ofReg(id, 4); }
Operand r64(int id) { return Operand::ofReg(id, 8); }

}

// **`mov $d, %R; cdq; idiv %R` (or `xor %edx, %edx; div %R`) becomes a
// multiply by the divisor's magic number**, eax and edx left as the divide
// leaves them; R, dead after, carries the product.
bool divideByConstant(Stream &s, Flow &f) {
    f.live(s);
    struct Site { int at; std::vector<Instr> seq; };
    std::vector<Site> sites;
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        const Block &blk = f.blocks[b];
        Live live = blk.out;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            if (s[k].kind != Entry::Ins || s[k].dead) continue;
            f.joinPads(b, k, live);
            const Live after = live;
            live.step(f.effects[k]);
            const Instr &dv = s[k].ins;
            if (!(dv.m == "idiv" || dv.m == "div") || dv.operands != 1 || !gpr(dv.a) || dv.a.reg.width != 4) continue;
            const int R = dv.a.reg.id;
            if (R == RAX || R == RDX || frameReg(R) || (after.regs & bit(R))) continue;
            int k1 = k - 1, k0;
            while (k1 >= blk.begin && s[k1].kind == Entry::Ins && s[k1].dead) --k1;
            k0 = k1 - 1;
            while (k0 >= blk.begin && s[k0].kind == Entry::Ins && s[k0].dead) --k0;
            if (k0 < blk.begin || s[k1].kind != Entry::Ins || s[k0].kind != Entry::Ins) continue;
            const Instr &ext = s[k1].ins, &mv = s[k0].ins;
            const bool sign = dv.m == "idiv";
            if (sign ? ext.m != "cdq" : !(ext.m == "xor" && ext.a.isReg(RDX) && ext.b.isReg(RDX) && ext.a.reg.width == 4)) continue;
            if (!isMovQL(mv.m) || mv.a.kind != Operand::Immediate || !mv.a.numeric || !gpr(mv.b) || mv.b.reg.id != R ||
                mv.b.reg.width < 4)
                continue;
            const long long d = mv.b.reg.width == 4 ? static_cast<unsigned>(mv.a.value) : mv.a.value;
            if (d < 2 || d >= (1LL << 31)) continue;
            unsigned long long M;
            int sh;
            if (sign) signedMagic(static_cast<unsigned>(d), M, sh);
            else if (!unsignedMagic(static_cast<unsigned>(d), M, sh)) continue;
            Site site{k0, {}};
            std::vector<Instr> &q = site.seq;
            q.push_back(two(sign ? "movslq" : "mov", r32(RAX), sign ? r64(RDX) : r32(RDX)));
            q.push_back(two("mov", Operand::ofImm(static_cast<long long>(M)), r32(R)));
            q.push_back(two("imul", r64(RDX), r64(R)));
            q.push_back(two(sign ? "sar" : "shr", Operand::ofImm(32 + sh), r64(R)));
            if (sign) {
                q.push_back(two("mov", r64(RDX), r64(RAX)));
                q.push_back(two("shr", Operand::ofImm(63), r64(RAX)));
                q.push_back(two("add", r32(R), r32(RAX)));
                q.push_back(two("mov", r32(RAX), r32(R)));
            } else {
                q.push_back(two("mov", r32(R), r32(RAX)));
            }
            q.push_back(two("imul", Operand::ofImm(d), r32(R)));
            q.push_back(two("sub", r32(R), r32(RDX)));
            s[k0].dead = s[k1].dead = s[k].dead = true;
            sites.push_back(site);
        }
    }
    if (sites.empty()) return false;
    std::stable_sort(sites.begin(), sites.end(), [](const Site &x, const Site &y) { return x.at > y.at; });
    for (const Site &site : sites) {
        std::vector<Entry> es;
        for (const Instr &i : site.seq) { Entry e; e.ins = i; es.push_back(e); }
        s.insert(s.begin() + site.at, es.begin(), es.end());
    }
    return true;
}

}
