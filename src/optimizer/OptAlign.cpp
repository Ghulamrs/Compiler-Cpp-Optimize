// **A loop that fits one line is placed so that it does not cross one**: the
// head of an innermost loop of L <= 64 estimated bytes gets `.p2align 6,,L-1`,
// which pads exactly when the L bytes from here would cross a 64-byte line.

#include "OptFunction.h"
#include "OptPasses.h"

#include <map>
#include <string>
#include <vector>

namespace opt {

namespace {

bool startsWith(const std::string &s, const char *p) { return s.compare(0, std::string(p).size(), p) == 0; }
bool endsWith(const std::string &s, const char *p) {
    const std::string t(p);
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
bool fitsByte(long long v) { return v >= -128 && v <= 127; }
bool fitsInt(long long v) { return v >= -(1LL << 31) && v < (1LL << 31); }

// An SSE instruction: a mandatory prefix and the 0F escape in front of the opcode.
bool sse(const std::string &m) {
    return endsWith(m, "sd") || endsWith(m, "ss") || endsWith(m, "pd") || endsWith(m, "ps") || startsWith(m, "cvt") ||
           startsWith(m, "ucomi") || startsWith(m, "comi") || m == "pxor" || m == "movd" || m == "movq";
}

// An instruction whose opcode has the 0F escape: imul r,r/m, the extensions, setcc, cmovcc.
bool twoByte(const std::string &m) {
    return m == "imul" || startsWith(m, "movs") || startsWith(m, "movz") || startsWith(m, "set") ||
           startsWith(m, "cmov") || startsWith(m, "bt");
}

int memoryBytes(const Operand &o) {
    int n = 0;
    if (o.scale != 0 || o.reg.id == RSP || o.reg.id == R12) n += 1;     // a SIB byte
    const bool needsDisp = o.hasDisp && o.disp != 0;
    if (needsDisp || o.reg.id == RBP || o.reg.id == R13) n += fitsByte(o.disp) ? 1 : 4;
    return n;
}

// The bytes of one instruction as it will most likely be encoded, erring long.
int bytesOf(const Instr &i) {
    const std::string &m = i.m;
    if (m[0] == 'j') return 2;                  // short: a loop this size exits nearby
    if (m == "call") return i.a.kind == Operand::Indirect ? 3 : 5;
    if (m == "ret") return 1;
    if (i.operands == 0) return 3;
    int n = 2;                                  // opcode and modrm
    if (sse(m)) n += 2;
    else if (twoByte(m)) n += 1;
    bool rex = false;
    for (const Operand *o : {&i.a, &i.b}) {
        if (o->kind == Operand::Register || o->kind == Operand::Memory || o->kind == Operand::Indirect)
            rex = rex || (o->kind == Operand::Register && o->reg.width == 8) || (o->reg.id >= R8 && o->reg.id < kGprs) ||
                  (o->kind == Operand::Memory && o->scale != 0 && o->index.id >= R8);
        switch (o->kind) {
        case Operand::Memory: n += memoryBytes(*o); break;
        case Operand::RipSymbol: n += 4; break;
        case Operand::Immediate:
            if (!o->numeric) n += 4;
            else if (m == "mov" || startsWith(m, "movabs")) n += fitsInt(o->value) ? 4 : 8;
            else n += fitsByte(o->value) ? 1 : 4;
            break;
        default: break;
        }
    }
    // A 64-bit register, or a numbered one, wants the REX prefix; so does a 64-bit move of a constant.
    if (rex || (m == "mov" && i.a.kind == Operand::Immediate && i.b.kind == Operand::Register && i.b.reg.width == 8)) n += 1;
    return n;
}

// The estimated bytes of the instructions in [from, to).
int bytesFrom(const Stream &s, int from, int to) {
    int n = 0;
    for (int k = from; k < to; ++k)
        if (!s[k].dead && s[k].kind == Entry::Ins) n += bytesOf(s[k].ins);
    return n;
}

// The bytes from `from` through the first unconditional jump or return, or of [from, to).
int runFrom(const Stream &s, int from, int to) {
    int n = 0;
    for (int k = from; k < to; ++k) {
        if (s[k].dead || s[k].kind != Entry::Ins) continue;
        n += bytesOf(s[k].ins);
        if (s[k].ins.m == "jmp" || s[k].ins.m == "ret") break;
    }
    return n;
}

// The first label of the block, before any instruction, or -1.
int headLabel(const Stream &s, const Block &b) {
    for (int k = b.begin; k < b.end; ++k) {
        if (s[k].dead) continue;
        if (s[k].kind == Entry::Label) return k;
        if (s[k].kind == Entry::Ins) return -1;
    }
    return -1;
}

}

bool alignLoops(Function &fn) {
    const int line = fn.costs().loopLine();
    const Loops &loops = fn.loops();
    std::vector<bool> isHead(fn.flow.blocks.size(), false);
    for (const Loops::Loop &loop : loops.all()) isHead[loop.head] = true;
    // The pad at each head's label: two back edges to one head are one loop here.
    std::map<int, int> pads;
    for (const Loops::Loop &loop : loops.all()) {
        // Innermost: no other loop's head among its blocks.
        bool innermost = true;
        int last = fn.flow.blocks[loop.head].end;
        for (int b : loop.blocks) {
            if (b != loop.head && isHead[b]) innermost = false;
            if (fn.flow.blocks[b].end > last) last = fn.flow.blocks[b].end;
        }
        const int at = headLabel(fn.stream, fn.flow.blocks[loop.head]);
        if (at < 0) continue;
        // The whole loop where it fits a line; otherwise the run every turn
        // begins with - from the head to the first jump that must be taken.
        int bytes = innermost ? bytesFrom(fn.stream, at, last) : 0;
        if (!innermost || bytes > line + line / 8) bytes = runFrom(fn.stream, at, last);
        if (bytes > line + line / 8) continue;
        // The estimate runs a few bytes long (measured: +0 to +9 on 14 loops), so one just over is padded as the line.
        if (bytes > line) bytes = line;
        if (bytes > 0 && bytes > pads[at]) pads[at] = bytes;
    }
    if (pads.empty()) return false;
    // Inserted from the back, so the earlier positions stay what they were.
    for (auto p = pads.rbegin(); p != pads.rend(); ++p) {
        Entry e;
        e.kind = Entry::Event;
        const int bytes = p->second;
        e.event = [bytes](Spelling &s) { s.loopAlign(bytes); };
        fn.stream.insert(fn.stream.begin() + p->first, e);
    }
    return true;
}

}
