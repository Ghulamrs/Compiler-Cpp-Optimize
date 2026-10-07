// The C6000 passes over one function's text: the -O0 NOPs are dropped, the sequential program is rewritten
// by the peepholes under a liveness of its blocks, and each block is list-scheduled into execute packets,
// the delay slots of its branch filled, and padded to the hazards that remain.

#include "C6xSched.h"
#include "C6xModel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <vector>

bool skipped(const char *pass);   // CPP11_C6XSKIP, below

namespace c6x {

bool startsWith(const std::string &s, const char *p) { return s.compare(0, std::string(p).size(), p) == 0; }
bool endsWith(const std::string &s, const char *p) {
    std::string q(p);
    return s.size() >= q.size() && s.compare(s.size() - q.size(), q.size(), q) == 0;
}

// Delay slots by mnemonic, as the -O0 backend pads them: the unknown floating
// forms take the longest of their precision rather than none.
int delaySlots(const std::string &m) {
    if (m == "B" || m == "CALLP") return 5;
    if (startsWith(m, "LD")) return 4;
    if (m == "MPYDP") return 9;
    if (m == "ADDDP" || m == "SUBDP") return 6;
    if (m == "INTDP" || m == "INTDPU") return 4;
    if (startsWith(m, "CMP") && (endsWith(m, "DP") || endsWith(m, "SP"))) return 1;
    if (m == "SPDP") return 1;
    if (m == "MPYSP" || m == "ADDSP" || m == "SUBSP") return 3;
    if (m == "INTSP" || m == "INTSPU" || m == "SPTRUNC" || m == "DPTRUNC" || m == "SPINT" || m == "DPINT" || m == "DPSP") return 3;
    if (startsWith(m, "MPY")) return 3;
    if (endsWith(m, "DP")) return 9;
    if (endsWith(m, "SP")) return 3;
    return 0;
}

// The C67x's double-precision instructions read a pair's high word after issue - one cycle later for ADDDP, SUBDP, CMPxxDP, MPYSPDP and the DPxx
// conversions, up to three for MPYDP, MPYI and MPYID - and hold their unit that long (SPRUFE8). TI's cycle-accurate simulator: 1.0 + 2.0 by ADDDP
// with A7 zeroed in its packet is 1, 3.0 * 2.0 by MPYDP with A7 zeroed in the next is 0 (tools/c6x-dphazards finds both in a schedule).
int lateReads(const std::string &m) {
    if (m == "MPYDP" || m == "MPYI" || m == "MPYID") return 3;
    if (m == "ADDDP" || m == "SUBDP" || m == "MPYSPDP" || m == "DPSP" || m == "DPINT" || m == "DPTRUNC") return 1;
    return startsWith(m, "CMP") && endsWith(m, "DP") ? 1 : 0;
}

Line parse(const std::string &raw) {
    Line l;
    l.raw = raw;
    if (raw.empty() || raw[0] != '\t') return l;        // a label, or blank
    std::size_t i = 1;
    if (i < raw.size() && raw[i] == '[') {
        std::size_t close = raw.find(']', i);
        if (close == std::string::npos) return l;
        l.pred = raw.substr(i + 1, close - i - 1);
        i = close + 1;
        while (i < raw.size() && raw[i] == '\t') i++;
    }
    if (i >= raw.size() || !std::isupper(static_cast<unsigned char>(raw[i]))) return l;   // a directive
    std::size_t tab = raw.find('\t', i);
    l.mnem = raw.substr(i, tab == std::string::npos ? std::string::npos : tab - i);
    if (tab != std::string::npos) {
        std::string rest = raw.substr(tab + 1);
        std::size_t at = 0;
        while (at <= rest.size()) {
            std::size_t comma = rest.find(", ", at);
            l.ops.push_back(rest.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
            if (comma == std::string::npos) break;
            at = comma + 2;
        }
    }
    l.instr = true;
    return l;
}

Line make(const std::string &mnem, const std::string &a, const std::string &b, const std::string &c) {
    std::string raw = "\t" + mnem + "\t" + a;
    if (!b.empty()) raw += ", " + b;
    if (!c.empty()) raw += ", " + c;
    return parse(raw);
}

// An instruction rebuilt from its parts, every operand kept, and the predicate with it.
Line rebuilt(const std::string &mnem, const std::vector<std::string> &ops, const std::string &pred) {
    std::string raw = "\t" + (pred.empty() ? std::string() : "[" + pred + "]\t") + mnem;
    for (std::size_t o = 0; o < ops.size(); o++) raw += (o == 0 ? "\t" : ", ") + ops[o];
    return parse(raw);
}

bool isNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '.'; }

// The registers an operand names: A0-A31 and B0-B31 as whole words, so a
// label or symbol that happens to contain one is not counted.
void registersIn(const std::string &op, std::vector<std::string> &out) {
    for (std::size_t i = 0; i < op.size(); i++) {
        if ((op[i] != 'A' && op[i] != 'B') || (i > 0 && isNameChar(op[i - 1]))) continue;
        std::size_t j = i + 1;
        while (j < op.size() && std::isdigit(static_cast<unsigned char>(op[j]))) j++;
        if (j == i + 1 || (j < op.size() && isNameChar(op[j]))) continue;
        out.push_back(op.substr(i, j - i));
        i = j - 1;
    }
}

bool isStore(const std::string &m) { return startsWith(m, "ST"); }
bool isLoad(const std::string &m) { return startsWith(m, "LD"); }

// A register's file: 'A' or 'B', or 0 for anything else.
char sideOf(const std::string &r) { return r.size() >= 2 && (r[0] == 'A' || r[0] == 'B') && std::isdigit(static_cast<unsigned char>(r[1])) ? r[0] : 0; }
int sideIndex(const std::string &r) { return sideOf(r) == 'B' ? 1 : 0; }

// A register as one bit of a 64-bit set: A0-A31 low, B0-B31 high.
std::uint64_t bitOf(const std::string &r) {
    if (!sideOf(r)) return 0;
    int n = std::atoi(r.c_str() + 1);
    if (n < 0 || n > 31) return 0;
    return 1ull << (n + (r[0] == 'B' ? 32 : 0));
}
std::uint64_t maskOf(const std::vector<std::string> &regs) {
    std::uint64_t m = 0;
    for (std::size_t i = 0; i < regs.size(); i++) m |= bitOf(regs[i]);
    return m;
}
std::uint64_t maskOf(const char *const *names, std::size_t n) {
    std::uint64_t m = 0;
    for (std::size_t i = 0; i < n; i++) m |= bitOf(names[i]);
    return m;
}

// What the instruction reads and writes: the last operand is the destination
// unless it is memory (a store) or a branch target, and MVKH keeps its low half.
void readsAndWrites(const Line &l, std::vector<std::string> &reads, std::vector<std::string> &writes) {
    if (!l.pred.empty()) reads.push_back(l.pred[0] == '!' ? l.pred.substr(1) : l.pred);
    if (l.ops.empty()) return;
    bool hasDest = !isStore(l.mnem) && l.mnem != "B" && l.mnem != "NOP" && l.ops.back()[0] != '*';
    std::size_t n = hasDest ? l.ops.size() - 1 : l.ops.size();
    for (std::size_t i = 0; i < n; i++) registersIn(l.ops[i], reads);
    for (std::size_t i = 0; i < l.ops.size(); i++)      // `*R++`, `*R--`: the access writes its address register too
        if (l.ops[i][0] == '*' && (l.ops[i].find("++") != std::string::npos || l.ops[i].find("--") != std::string::npos)) registersIn(l.ops[i], writes);
    if (!hasDest) return;
    registersIn(l.ops.back(), writes);
    if (l.mnem == "MVKH" || l.mnem == "ADDK") registersIn(l.ops.back(), reads);
}

// The address register a `*R++(k)` or `*R--(k)` operand steps, and by how many bytes (signed), or "" for none.
std::string steppedRegister(const Line &l, long &by) {
    by = 0;
    for (std::size_t i = 0; i < l.ops.size(); i++) {
        const std::string &m = l.ops[i];
        std::size_t p = m.find("++");
        if (m.empty() || m[0] != '*' || (p == std::string::npos && (p = m.find("--")) == std::string::npos)) continue;
        std::vector<std::string> regs;
        registersIn(m, regs);
        if (regs.size() != 1) return "";
        std::size_t open = m.find('(', p);
        by = open == std::string::npos ? 1 : std::atol(m.substr(open + 1).c_str());
        if (m[p] == '-') by = -by;
        return regs[0];
    }
    return "";
}

bool has(const std::vector<std::string> &v, const std::string &r) {
    for (std::size_t i = 0; i < v.size(); i++) if (v[i] == r) return true;
    return false;
}

bool isNumber(const std::string &s) {
    std::size_t i = s.size() > 1 && s[0] == '-' ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); i++) if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

bool isLabel(const Line &l) { return !l.instr && !l.raw.empty() && l.raw[0] != '\t'; }
std::string labelName(const Line &l) { return l.raw.substr(0, l.raw.find(':')); }
// CALLP is a branch that ends its block too, and a call: A4's rule that no push/pop pair spans a call rests on it.
bool isBranch(const std::string &m) { return m == "B" || m == "CALLP"; }
bool blockEnd(const Line &l) { return !l.instr || isBranch(l.mnem); }

// ----- the calls, the returns and what they read and write -----

const char *const kArgRegs[] = { "A3", "A4", "A5", "B4", "B5", "A6", "A7", "B6", "B7", "A8", "A9", "B8", "B9",
                                 "A10", "A11", "B10", "B11", "A12", "A13", "B12", "B13", "B3", "A15", "B15", "B14" };
const char *const kExitRegs[] = { "A4", "A5", "A10", "A11", "A12", "A13", "A14", "A15", "B3", "B10", "B11", "B12", "B13", "B14", "B15" };

// The registers a call reads (the EABI's argument registers, A3 the hidden result pointer, B3, the stack pointers) and clobbers.
std::uint64_t callReads() { return maskOf(kArgRegs, sizeof kArgRegs / sizeof kArgRegs[0]); }
std::uint64_t callWrites() {
    std::uint64_t m = 0;
    for (int n = 0; n < 32; n++) if (n <= 9 || n >= 16) m |= (1ull << n) | (1ull << (n + 32));
    return m;
}
std::uint64_t exitLive() { return maskOf(kExitRegs, sizeof kExitRegs / sizeof kExitRegs[0]); }

// A branch to a symbol that is no label of this text, or through a register other than B3, is a call.
bool isCall(const Line &l, const std::set<std::string> &labels) {
    if (l.mnem == "CALLP") return true;
    if (l.mnem != "B" || l.ops.empty()) return false;
    if (sideOf(l.ops[0])) return l.ops[0] != "B3";
    return labels.count(l.ops[0]) == 0 && !startsWith(l.ops[0], "L$return$") && !startsWith(l.ops[0], "L.return.");
}
bool isReturn(const Line &l, const std::set<std::string> &labels) {
    if (l.mnem != "B" || l.ops.empty()) return false;
    if (sideOf(l.ops[0])) return l.ops[0] == "B3";
    return labels.count(l.ops[0]) == 0 && (startsWith(l.ops[0], "L$return$") || startsWith(l.ops[0], "L.return."));
}

// ----- liveness over the blocks -----

// **The guard.** Liveness is a cache of one text: it keeps the text's buffer, its length and the edit count it was
// computed at, and a question asked of it after an edit recomputes it first - so a pass that removes a definition and
// then asks whether a register is free cannot read the answer from before the removal (numberValues, 2026-10-07).
thread_local std::uint64_t textEdits = 0;
namespace {
struct LiveStamp { const Line *data = nullptr; std::size_t size = 0; std::uint64_t edits = ~0ull; };
thread_local LiveStamp liveStamp;
thread_local const char *heldBy = nullptr;     // the pass inside a LivenessHeld, if any
bool liveCurrent(const std::vector<Line> &v) {
    return liveStamp.data == v.data() && liveStamp.size == v.size() && liveStamp.edits == textEdits;
}
// Recompute where stale: the fields are a cache, the vector is never a const object, and nothing but them is written.
void ensureLiveness(const std::vector<Line> &v) {
    if (!liveCurrent(v)) computeLiveness(const_cast<std::vector<Line> &>(v));
}
bool liveChecking() { static const bool on = std::getenv("CPP11_C6XLIVECHECK") != nullptr; return on; }
}   // namespace

LivenessHeld::LivenessHeld(const std::vector<Line> &v, const char *pass) : outer(heldBy) { if (!heldBy) ensureLiveness(v); heldBy = pass; }
LivenessHeld::~LivenessHeld() { heldBy = outer; }

std::uint64_t liveOutAt(const std::vector<Line> &v, std::size_t i) { if (!heldBy) ensureLiveness(v); return v[i].liveOut; }
std::uint64_t liveInAt(const std::vector<Line> &v, std::size_t i) { if (!heldBy) ensureLiveness(v); return v[i].liveIn; }

// Every line takes the registers live at the end of its block: a label or a
// directive opens a block, a branch closes one, a call reads its arguments
// and clobbers the caller-saved registers, and a return leaves the exit set.
void computeLiveness(std::vector<Line> &v) {
    std::set<std::string> labels;
    for (std::size_t i = 0; i < v.size(); i++) if (isLabel(v[i])) labels.insert(labelName(v[i]));
    std::vector<std::size_t> starts;                       // the first line of each block
    for (std::size_t i = 0; i < v.size(); i++)
        if (i == 0 || isLabel(v[i]) || !v[i].instr || blockEnd(v[i - 1])) starts.push_back(i);
    std::size_t nb = starts.size();
    std::map<std::string, std::size_t> blockOfLabel;
    for (std::size_t b = 0; b < nb; b++) if (isLabel(v[starts[b]])) blockOfLabel[labelName(v[starts[b]])] = b;
    std::vector<std::uint64_t> use(nb, 0), def(nb, 0), in(nb, 0), out(nb, 0);
    std::vector<std::vector<std::size_t> > succ(nb);
    std::vector<bool> exits(nb, false), unknown(nb, false);
    for (std::size_t b = 0; b < nb; b++) {
        std::size_t end = b + 1 < nb ? starts[b + 1] : v.size();
        bool fall = true;
        for (std::size_t i = starts[b]; i < end; i++) {
            const Line &l = v[i];
            if (!l.instr) continue;
            std::vector<std::string> reads, writes;
            readsAndWrites(l, reads, writes);
            std::uint64_t r = maskOf(reads), w = maskOf(writes);
            if (isBranch(l.mnem)) {
                if (isCall(l, labels)) { r |= callReads(); w = callWrites(); if (l.mnem == "CALLP") { r &= ~bitOf("B3"); w |= bitOf("B3"); } }
                else if (isReturn(l, labels)) { exits[b] = true; fall = false; }
                else if (labels.count(l.ops[0])) { succ[b].push_back(blockOfLabel[l.ops[0]]); if (l.pred.empty()) fall = false; }
                else { unknown[b] = true; if (l.pred.empty()) fall = false; }
            }
            use[b] |= r & ~def[b];
            if (l.pred.empty()) def[b] |= w;
        }
        if (fall && b + 1 < nb) succ[b].push_back(b + 1);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t b = nb; b-- > 0;) {
            std::uint64_t o = exits[b] ? exitLive() : 0;
            if (unknown[b] || v[starts[b]].verbatim) o = ~0ull;
            for (std::size_t k = 0; k < succ[b].size(); k++) o |= in[succ[b][k]];
            std::uint64_t n = v[starts[b]].verbatim ? ~0ull : use[b] | (o & ~def[b]);
            if (o != out[b] || n != in[b]) { out[b] = o; in[b] = n; changed = true; }
        }
    }
    for (std::size_t b = 0; b < nb; b++) {
        std::size_t end = b + 1 < nb ? starts[b + 1] : v.size();
        for (std::size_t i = starts[b]; i < end; i++) { v[i].liveOut = out[b]; v[i].liveIn = in[b]; }
    }
    liveStamp.data = v.data(); liveStamp.size = v.size(); liveStamp.edits = textEdits;
}

// Whether reg is overwritten before it is read on the straight path from i, or
// is not live where the block ends; a directive ends the path and answers no.
bool deadAfterAsComputed(const std::vector<Line> &v, std::size_t i, const std::string &reg) {
    for (std::size_t j = i + 1; j < v.size(); j++) {
        if (!v[j].instr) return isLabel(v[j]) ? (v[i].liveOut & bitOf(reg)) == 0 : false;
        std::vector<std::string> reads, writes;
        readsAndWrites(v[j], reads, writes);
        if (has(reads, reg)) return false;
        if (v[j].mnem == "CALLP") {
            if (callReads() & bitOf(reg)) return false;          // a call: its arguments
            return (callWrites() & bitOf(reg)) || reg == "B3" || (v[j].liveOut & bitOf(reg)) == 0;
        }
        if (v[j].mnem == "B") {
            if (sideOf(v[j].ops[0]) ? v[j].ops[0] != "B3" : !startsWith(v[j].ops[0], "L$") && !startsWith(v[j].ops[0], "L.")) {
                if (callReads() & bitOf(reg)) return false;      // a call: its arguments
                if (callWrites() & bitOf(reg)) return true;      // clobbered by the callee
            }
            return (v[j].liveOut & bitOf(reg)) == 0;
        }
        if (has(writes, reg) && v[j].pred.empty()) return true;
    }
    return (v[i].liveOut & bitOf(reg)) == 0;
}

// The question every pass asks, of liveness that is current - or, inside a LivenessHeld, as it was computed, which
// CPP11_C6XLIVECHECK=1 holds to its vow: a "dead" the current text would answer "live" to stops the compiler.
bool deadAfter(const std::vector<Line> &v, std::size_t i, const std::string &reg) {
    if (!heldBy) { ensureLiveness(v); return deadAfterAsComputed(v, i, reg); }
    const bool dead = deadAfterAsComputed(v, i, reg);
    if (dead && liveChecking() && !liveCurrent(v)) {
        const std::uint64_t edits = textEdits;
        const LiveStamp stamp = liveStamp;
        std::vector<Line> now(v);
        computeLiveness(now);
        const bool really = deadAfterAsComputed(now, i, reg);
        textEdits = edits; liveStamp = stamp;
        if (!really) {
            std::fprintf(stderr, "cpp11: liveness held by %s is stale: %s is live after line %zu,%s\n", heldBy, reg.c_str(), i, v[i].raw.c_str());
            std::abort();
        }
    }
    return dead;
}

// MVKL then MVKH of a constant that fits sixteen signed bits is one MVK.
void foldMvk(std::vector<Line> &v) {
    for (std::size_t i = 0; i + 1 < v.size(); i++) {
        const Line &a = v[i], &b = v[i + 1];
        if (a.mnem != "MVKL" || b.mnem != "MVKH" || a.ops.size() != 2 || a.ops != b.ops || !isNumber(a.ops[0])) continue;
        long k = std::atol(a.ops[0].c_str());
        if (k < -32768 || k > 32767) continue;
        v[i] = make("MVK", a.ops[0], a.ops[1]);
        v.erase(v.begin() + static_cast<long>(i) + 1);
    }
}

int accessSize(const std::string &m) {
    if (m == "LDDW" || m == "STDW") return 8;   // LDW ends in DW too
    if (endsWith(m, "W")) return 4;
    if (endsWith(m, "H") || endsWith(m, "HU")) return 2;
    return 1;
}

// A local's address built and used once becomes the *-A15(k) form where k
// fits ucst5 scaled by the access: MVK k,A0; SUB A15,A0,R; LDW *R,D, and
// the SUB A15,k,R spelling of a small k, each with R and A0 dead after.
void foldFrame(std::vector<Line> &v) {
    LivenessHeld held(v, "foldFrame");   // It removes only definitions of A0 and of an address register, each dead where it goes.
    for (std::size_t i = 0; i + 1 < v.size(); i++) {
        if ((v[i].mnem != "SUB" && v[i].mnem != "SUBAW") || v[i].ops.size() != 3 || v[i].ops[0] != "A15") continue;
        std::string reg = v[i].ops[2];
        long k;
        std::size_t first = i;
        if (v[i].mnem == "SUBAW") { if (!isNumber(v[i].ops[1])) continue; k = 4 * std::atol(v[i].ops[1].c_str()); }
        else if (isNumber(v[i].ops[1])) k = std::atol(v[i].ops[1].c_str());
        else if (v[i].ops[1] == "A0" && i > 0 && v[i - 1].mnem == "MVK" && v[i - 1].ops.size() == 2 &&
                 v[i - 1].ops[1] == "A0" && isNumber(v[i - 1].ops[0]) && deadAfter(v, i, "A0")) {
            k = std::atol(v[i - 1].ops[0].c_str());
            first = i - 1;
        } else continue;
        Line &use = v[i + 1];
        if (!use.instr || use.ops.size() != 2 || !use.pred.empty()) continue;
        bool load = startsWith(use.mnem, "LD") && use.ops[0] == "*" + reg;
        bool store = isStore(use.mnem) && use.ops[1] == "*" + reg;
        if (!load && !store) continue;
        int size = accessSize(use.mnem);
        if (k < 0 || k % size != 0) continue;
        // A load into its own address register consumes the address itself: `LDW *A4, A4`.
        std::vector<std::string> into;
        if (load) registersIn(use.ops[1], into);
        if (!has(into, reg) && !deadAfter(v, i + 1, reg)) continue;
        // Past ucst5, the scaled register offset: MVK k/size, A0; LDW *-A15[A0], D - A0 was dead after anyway.
        const bool far = k / size > 31;
        if (far && (first == i || k / size > 32767)) continue;
        std::string mem = far ? "*-A15[A0]" : "*-A15(" + std::to_string(k) + ")";
        v[i + 1] = load ? make(use.mnem, mem, use.ops[1]) : make(use.mnem, use.ops[0], mem);
        if (far) {
            v[first] = make("MVK", std::to_string(k / size), "A0");
            v.erase(v.begin() + static_cast<long>(i));
        } else {
            v.erase(v.begin() + static_cast<long>(first), v.begin() + static_cast<long>(i) + 1);
            i = first;
        }
    }
}

std::string renamed(const std::string &op, const std::string &from, const std::string &to) {
    std::string out;
    for (std::size_t i = 0; i < op.size();) {
        std::size_t j = i;
        if ((op[i] == 'A' || op[i] == 'B') && (i == 0 || !isNameChar(op[i - 1]))) {
            j = i + 1;
            while (j < op.size() && std::isdigit(static_cast<unsigned char>(op[j]))) j++;
            if (j > i + 1 && (j >= op.size() || !isNameChar(op[j])) && op.substr(i, j - i) == from) {
                out += to;
                i = j;
                continue;
            }
        }
        out += op[i++];
    }
    return out;
}

// Whether operand o of this instruction may come over the cross path: TI's src2 - the second source of the
// arithmetic, either of a commutative one or a compare (asm6x turns them about), the value of a shift or EXT
// (written first) - and never a count, a field instruction's source (EXTU, CLR), or a conversion's.
bool mayCross(const std::string &m, std::size_t o) {
    if (m == "ADD" || m == "AND" || m == "OR" || m == "XOR" || m == "SUB" || startsWith(m, "CMP")) return true;
    if (m == "MV" || m == "NEG" || m == "NOT" || m == "SPDP" || m == "INTSP" || m == "INTDP" || m == "SPTRUNC") return true;
    if (m == "SHL" || m == "SHR" || m == "SHRU") return o == 0;
    if (startsWith(m, "MPY") || m == "ADDSP" || m == "SUBSP" || m == "ADDDP" || m == "SUBDP") return o == 1;
    return false;
}

unsigned unitsFor(const Line &l);

// A pair crosses only as the second source of MPYDP, ADDDP or SUBDP - the xdp forms ASM6x's table has (ADDDP and SUBDP on .S alone).
bool pairMayCross(const std::string &m) { return m == "MPYDP" || m == "ADDDP" || m == "SUBDP"; }

// How many of an instruction's source registers sit on the other side from
// its destination - at most one may, over the cross path, and only in a
// position that has a crossed form; 2 answers for anything illegal.
int crossings(const Line &l) {
    if (l.ops.empty() || isStore(l.mnem) || isLoad(l.mnem) || isBranch(l.mnem)) return 0;
    std::vector<std::string> dst;
    registersIn(l.ops.back(), dst);
    if (dst.empty()) return 0;
    int n = 0;
    for (std::size_t o = 0; o + 1 < l.ops.size(); o++) {
        std::vector<std::string> regs;
        registersIn(l.ops[o], regs);
        bool crossed = false;
        for (std::size_t k = 0; k < regs.size(); k++) if (sideOf(regs[k]) != sideOf(dst[0])) crossed = true;
        if (crossed && (!mayCross(l.mnem, o) || (regs.size() > 1 && !pairMayCross(l.mnem)))) return 2;
        if (crossed) n++;
    }
    return n;
}

// The aligned pair `odd:even` the two registers make, lo the even one, or "" where they are not one pair of one side.
std::string pairOf(const std::string &lo, const std::string &hi) {
    if (!sideOf(lo) || sideOf(lo) != sideOf(hi)) return "";
    int a = std::atoi(lo.c_str() + 1), b = std::atoi(hi.c_str() + 1);
    return a % 2 == 0 && b == a + 1 ? hi + ":" + lo : "";
}

// A rebuilt instruction with one legal crossing at most: a commutative one turns its sources about where the first crosses.
bool legalForm(Line &r) {
    const std::string &m = r.mnem;
    const bool commutes = m == "ADD" || m == "AND" || m == "OR" || m == "XOR" || m == "MPYDP" || m == "ADDDP" || m == "MPYSP" || m == "ADDSP" || m == "MPY32" || m == "CMPEQ";
    if (crossings(r) > 1 && commutes && r.ops.size() == 3 && !isNumber(r.ops[0]) && !isNumber(r.ops[1])) {
        std::vector<std::string> ops = r.ops;
        std::swap(ops[0], ops[1]);
        r = rebuilt(m, ops, r.pred);
    }
    return crossings(r) <= 1 && (unitsFor(r) != 0 || isLoad(m) || isStore(m));
}

// A register-to-register instruction whose operands may be renamed: no
// memory operand, and no pair - a half of A5:A4 cannot be renamed alone.
bool renameable(const Line &l) {
    if (!l.instr || isBranch(l.mnem) || isStore(l.mnem) || isLoad(l.mnem) || l.mnem == "ADDK") return false;   // ADDK reads its destination
    for (std::size_t o = 0; o < l.ops.size(); o++) if (l.ops[o].find(':') != std::string::npos || l.ops[o][0] == '*') return false;
    return true;
}

// **A copy's one reader takes the source itself**, on the straight path: `MV S, D` then the first reader of D, neither
// S nor D written between and D dead after it - across the register files too, where the reader is left with one crossing
// source. And the other way: an instruction whose result is only copied on writes the copy's destination itself.
bool forwardMoves(std::vector<Line> &v) {
    LivenessHeld held(v, "forwardMoves");   // A copy goes only where its destination's old value is read by nobody, its source read later in the same block.
    bool changed = false;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &mv = v[i];
        if (mv.mnem != "MV" || mv.ops.size() != 2 || !mv.pred.empty()) continue;
        const std::string S = mv.ops[0], D = mv.ops[1];
        if (sideOf(S) && S == D) { v.erase(v.begin() + static_cast<long>(i)); i--; changed = true; continue; }   // a self-copy
        if (!sideOf(S) || !sideOf(D) || S == "A15" || D == "A15" || S == "B15" || D == "B15") continue;
        // Retarget: the instruction before made S for this copy alone.
        if (i > 0 && renameable(v[i - 1]) && v[i - 1].pred.empty() && v[i - 1].mnem != "MVKH" &&
            !v[i - 1].ops.empty() && v[i - 1].ops.back() == S && deadAfter(v, i, S)) {
            std::vector<std::string> reads, writes;
            readsAndWrites(v[i - 1], reads, writes);
            if (writes.size() == 1) {
                Line p = v[i - 1];
                p.ops.back() = D;
                Line r = rebuilt(p.mnem, p.ops);
                if (crossings(r) <= 1 && unitsFor(r) != 0) {
                    v[i - 1] = r;
                    v.erase(v.begin() + static_cast<long>(i));
                    i--;
                    changed = true;
                    continue;
                }
            }
        }
        for (std::size_t j = i + 1; j < v.size(); j++) {
            if (blockEnd(v[j])) break;
            std::vector<std::string> reads, writes;
            readsAndWrites(v[j], reads, writes);
            if (has(reads, D)) {
                if (!v[j].pred.empty() || (!has(writes, D) && !deadAfter(v, j, D))) break;
                if (v[j].mnem == "ADDK" || v[j].mnem == "MVKH") break;      // the destination is read too
                Line u = v[j];
                bool pair = false;          // a register pair is odd:even, and a half cannot be renamed alone
                for (std::size_t o = 0; o < u.ops.size(); o++) if (u.ops[o].find(':') != std::string::npos) pair = true;
                if (pair) break;
                const std::size_t last = isStore(u.mnem) || u.mnem == "B" || u.ops.back()[0] == '*' ? u.ops.size() : u.ops.size() - 1;
                for (std::size_t o = 0; o < last; o++) u.ops[o] = renamed(u.ops[o], D, S);
                for (std::size_t o = last; o < u.ops.size(); o++)
                    if (u.ops[o][0] == '*') u.ops[o] = renamed(u.ops[o], D, S);
                Line r = rebuilt(u.mnem, u.ops, u.pred);
                // A memory operand's registers stay on the unit's side; a branch register stays B.
                bool memSide = false;
                for (std::size_t o = 0; o < r.ops.size(); o++)
                    if (r.ops[o][0] == '*') { std::vector<std::string> m; registersIn(r.ops[o], m); if (m.size() > 1 && sideOf(m[0]) != sideOf(m[1])) memSide = true; }
                if (memSide || crossings(r) > 1 || unitsFor(r) == 0 || (r.mnem == "B" && sideOf(r.ops[0]) == 'A')) break;
                v[j] = r;
                v.erase(v.begin() + static_cast<long>(i));
                i--;
                changed = true;
                break;
            }
            if (has(writes, D) || has(writes, S)) break;
        }
    }
    // A copy nothing reads before it is written again.
    for (std::size_t i = 0; i < v.size(); i++)
        if (v[i].mnem == "MV" && v[i].ops.size() == 2 && v[i].pred.empty() && sideOf(v[i].ops[1]) &&
            v[i].ops[1] != "A15" && v[i].ops[1] != "B15" && deadAfter(v, i, v[i].ops[1])) {
            v.erase(v.begin() + static_cast<long>(i));
            i--;
            changed = true;
        }
    return changed;
}

// The other half of an aligned pair, and the register it is copied from or to beside this copy: `MV S, L` wants `MV S^1, L^1`.
std::string partner(const std::string &r) {
    if (!sideOf(r)) return "";
    int n = std::atoi(r.c_str() + 1);
    return std::string(1, r[0]) + std::to_string(n ^ 1);
}

// Whether line j reads or writes any of the registers named, the predicate included.
bool touches(const Line &l, const std::vector<std::string> &regs) {
    std::vector<std::string> reads, writes;
    readsAndWrites(l, reads, writes);
    for (std::size_t k = 0; k < regs.size(); k++) if (has(reads, regs[k]) || has(writes, regs[k])) return true;
    return false;
}

// **A pair copied whole is made where the copies go**: `W ..., H:L` then `MV L, X` and `MV H, Y`, Y:X one aligned pair the
// lines between leave alone and L, H dead after the two, writes Y:X itself; and `MV S, L; MV T, H` then the first reader of
// L or H, reading them as the pair H:L and leaving them dead, reads T:S. The backend's A5:A4 result, moved into a temporary.
bool forwardPairMoves(std::vector<Line> &v) {
    LivenessHeld held(v, "forwardPairMoves");   // As forwardMoves, a half at a time - copies of values nobody reads after, the sources read within the block.
    bool changed = false;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &w = v[i];
        if (!w.instr || !w.pred.empty() || isStore(w.mnem) || isBranch(w.mnem) || w.mnem == "MVKH" || w.mnem == "ADDK" || w.ops.empty()) continue;
        if (w.ops.back().find(':') == std::string::npos) continue;
        std::vector<std::string> dst, reads, writes;
        registersIn(w.ops.back(), dst);
        readsAndWrites(w, reads, writes);
        if (dst.size() != 2 || writes.size() != 2) continue;
        const std::string H = dst[0], L = dst[1];
        std::size_t mL = 0, mH = 0;
        std::string X, Y;
        std::vector<std::string> halves; halves.push_back(L); halves.push_back(H);
        for (std::size_t j = i + 1; j < v.size() && !blockEnd(v[j]) && (!mL || !mH); j++) {
            const Line &m = v[j];
            if (m.mnem == "MV" && m.ops.size() == 2 && m.pred.empty() && sideOf(m.ops[1])) {
                if (m.ops[0] == L && !mL) { mL = j; X = m.ops[1]; continue; }
                if (m.ops[0] == H && !mH) { mH = j; Y = m.ops[1]; continue; }
            }
            if (touches(m, halves)) break;
        }
        if (!mL || !mH) continue;
        const std::string pair = pairOf(X, Y);
        if (pair.empty() || X == L || X == H || Y == L || Y == H || X == "A15" || X == "B15") continue;
        const std::size_t last = std::max(mL, mH);
        std::vector<std::string> both; both.push_back(X); both.push_back(Y);
        bool ok = deadAfter(v, last, L) && deadAfter(v, last, H);
        for (std::size_t k = i + 1; k < last && ok; k++) if (k != mL && k != mH && touches(v[k], both)) ok = false;
        if (!ok) continue;
        Line r = w;
        r.ops.back() = pair;
        r = rebuilt(r.mnem, r.ops, r.pred);
        if (!legalForm(r)) continue;
        v[i] = r;
        v.erase(v.begin() + static_cast<long>(last));
        v.erase(v.begin() + static_cast<long>(std::min(mL, mH)));
        changed = true;
    }
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &a = v[i];
        if (!a.instr || a.mnem != "MV" || a.ops.size() != 2 || !a.pred.empty() || !sideOf(a.ops[0]) || !sideOf(a.ops[1])) continue;
        const std::string S = a.ops[0], L = a.ops[1], T = partner(S), H = partner(L);
        if (S == "A15" || S == "B15" || L == "A15" || L == "B15" || T == "A15" || T == "B15" || sideOf(S) != sideOf(T)) continue;
        std::vector<std::string> four; four.push_back(S); four.push_back(T); four.push_back(L); four.push_back(H);
        std::size_t j = i + 1;
        for (; j < v.size() && !blockEnd(v[j]); j++) {
            const Line &b = v[j];
            if (b.mnem == "MV" && b.ops.size() == 2 && b.pred.empty() && b.ops[0] == T && b.ops[1] == H) break;
            if (touches(b, four)) { j = v.size(); break; }
        }
        // Stopped at a block's end, not at the partner copy: the label there is no copy to remove.
        if (j >= v.size() || blockEnd(v[j])) continue;
        const std::string from = pairOf(std::min(L, H), std::max(L, H)), to = pairOf(std::min(S, T), std::max(S, T));
        if (from.empty() || to.empty()) continue;
        std::size_t r = j + 1;
        for (; r < v.size() && !blockEnd(v[r]); r++) {
            std::vector<std::string> reads, writes;
            readsAndWrites(v[r], reads, writes);
            if (has(reads, L) || has(reads, H)) break;
            if (has(writes, L) || has(writes, H) || has(writes, S) || has(writes, T)) { r = v.size(); break; }
        }
        if (r >= v.size() || !v[r].instr || !v[r].pred.empty()) continue;
        Line u = v[r];
        std::vector<std::string> reads, writes;
        readsAndWrites(u, reads, writes);
        const bool hasDest = !isStore(u.mnem) && u.ops.back()[0] != '*';
        bool ok = u.mnem != "MVKH" && u.mnem != "ADDK", seen = false;
        for (std::size_t o = 0; o + (hasDest ? 1 : 0) < u.ops.size() && ok; o++) {
            if (u.ops[o] == from) { u.ops[o] = to; seen = true; continue; }
            std::vector<std::string> regs;                                      // a half read on its own stays a copy
            registersIn(u.ops[o], regs);
            if (has(regs, L) || has(regs, H)) ok = false;
        }
        const bool overwrites = hasDest && u.ops.back() == from;
        if (!ok || !seen || (!overwrites && (has(writes, L) || has(writes, H)))) continue;
        if (!overwrites && !(deadAfter(v, r, L) && deadAfter(v, r, H))) continue;
        Line nu = rebuilt(u.mnem, u.ops, u.pred);
        if (!legalForm(nu)) continue;
        v[r] = nu;
        v.erase(v.begin() + static_cast<long>(j));
        v.erase(v.begin() + static_cast<long>(i));
        i--;
        changed = true;
    }
    return changed;
}

// **A small constant goes into its one reader**: `MVK k, R` then the first reader of R, R
// dead after, where the reader has a constant form - ADD, AND, OR, XOR and the compares
// take -16..15 in either place, SUB r,k takes 0..31, a shift takes a count of 0..31.
bool forwardConstants(std::vector<Line> &v) {
    LivenessHeld held(v, "forwardConstants");   // A constant goes only where its register is dead after its one reader.
    bool changed = false;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &mk = v[i];
        if (mk.mnem != "MVK" || mk.ops.size() != 2 || !mk.pred.empty() || !isNumber(mk.ops[0])) continue;
        const long k = std::atol(mk.ops[0].c_str());
        const std::string R = mk.ops[1];
        for (std::size_t j = i + 1; j < v.size(); j++) {
            if (blockEnd(v[j])) break;
            std::vector<std::string> reads, writes;
            readsAndWrites(v[j], reads, writes);
            if (has(reads, R)) {
                const Line &u = v[j];
                if (!u.pred.empty() || !renameable(u) || u.ops.size() != 3 || (!has(writes, R) && !deadAfter(v, j, R))) break;
                const std::string &m = u.mnem;
                bool ok = false;
                std::vector<std::string> ops = u.ops;
                int at = ops[0] == R ? 0 : ops[1] == R ? 1 : -1;
                if (at < 0 || ops[0] == ops[1] || isNumber(ops[1 - at])) break;
                bool small = k >= -16 && k <= 15, ucst = k >= 0 && k <= 31;
                // A commutative operation takes its constant first, the spelling both assemblers read.
                bool first = m == "ADD" || m == "AND" || m == "OR" || m == "XOR";
                if (first && small) ok = true;
                else if ((m == "CMPEQ" || m == "CMPGT" || m == "CMPLT") && small && at == 0) ok = true;
                else if ((m == "CMPGTU" || m == "CMPLTU") && ucst && at == 0) ok = true;
                else if (m == "SUB" && ucst && at == 1 && crossings(u) == 0) ok = true;
                else if ((m == "SHL" || m == "SHR" || m == "SHRU") && ucst && at == 1) ok = true;
                if (!ok) break;
                if (first && at == 1) { ops[1] = ops[0]; at = 0; }
                ops[static_cast<std::size_t>(at)] = std::to_string(k);
                v[j] = rebuilt(m, ops);
                v.erase(v.begin() + static_cast<long>(i));
                i--;
                changed = true;
                break;
            }
            if (has(writes, R)) break;
        }
    }
    return changed;
}

// **A negated truth read only by a branch flips the branch instead**: `XOR 1, R, P` feeding
// `[P] B` or `[!P] B` next, P dead after the branch, becomes the copy `MV R, P` - or nothing
// where P is R - under the branch of the other sense.
bool flipPredicates(std::vector<Line> &v) {
    LivenessHeld held(v, "flipPredicates");   // The XOR becomes a copy with the same reads and writes, or goes where it read and wrote one register.
    bool changed = false;
    for (std::size_t i = 0; i + 1 < v.size(); i++) {
        const Line &x = v[i];
        if (x.mnem != "XOR" || x.ops.size() != 3 || x.ops[0] != "1" || !sideOf(x.ops[1]) || !x.pred.empty()) continue;
        const std::string R = x.ops[1], P = x.ops[2];
        const Line &b = v[i + 1];
        if (b.mnem != "B" || b.pred.empty() || (b.pred[0] == '!' ? b.pred.substr(1) : b.pred) != P) continue;
        if (!deadAfter(v, i + 1, P)) continue;
        v[i + 1] = rebuilt(b.mnem, b.ops, b.pred[0] == '!' ? b.pred.substr(1) : "!" + b.pred);
        if (R == P) v.erase(v.begin() + static_cast<long>(i));
        else v[i] = make("MV", R, P);
        changed = true;
    }
    return changed;
}

bool passThrough(const Line &l, const std::set<std::string> &named);

// **A branch to a test it has just made goes where that test goes**: `MV X, P; [P] B L` where L's first
// instruction is `MV X, P` again and the next `[P] B M` - the same sense - is taken at L for certain, so it
// goes to M; and `[P] B L` where L is `[P] B M` outright likewise. What `a && b` in a condition leaves behind.
bool threadPredicates(std::vector<Line> &v) {
    std::map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < v.size(); i++) if (isLabel(v[i])) at[labelName(v[i])] = i;
    bool changed = false;
    for (std::size_t i = 0; i < v.size(); i++) {
        Line &b = v[i];
        if (!b.instr || b.mnem != "B" || b.pred.empty() || !at.count(b.ops[0])) continue;
        const Line *mv = i > 0 && v[i - 1].instr && v[i - 1].mnem == "MV" && v[i - 1].ops.size() == 2 &&
                         v[i - 1].pred.empty() && v[i - 1].ops[1] == (b.pred[0] == '!' ? b.pred.substr(1) : b.pred) ? &v[i - 1] : nullptr;
        for (int hops = 0; hops < 8; hops++) {
            std::size_t j = at[b.ops[0]] + 1;
            while (j < v.size() && isLabel(v[j])) j++;
            if (j >= v.size() || !v[j].instr) break;
            if (v[j].mnem == "MV" && mv != nullptr && v[j].ops == mv->ops && v[j].pred.empty()) j++;
            if (j >= v.size() || !v[j].instr || v[j].mnem != "B" || v[j].pred != b.pred || !at.count(v[j].ops[0]) || v[j].ops[0] == b.ops[0]) break;
            b = rebuilt("B", std::vector<std::string>(1, v[j].ops[0]), b.pred);
            changed = true;
        }
    }
    return changed;
}

// A label of the backend's own flow that no branch names any longer - a `shortcut` threaded past, the return
// label of a function whose one return falls into it - ends no block, so the epilogue's `B B3` can be hoisted
// over the body's tail and the tail's results land in its delay slots.
void dropUnnamedLabels(std::vector<Line> &v) {
    std::set<std::string> named;
    for (std::size_t i = 0; i < v.size(); i++) if (v[i].instr) for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    std::vector<Line> out;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i]) && (passThrough(v[i], named) || (startsWith(labelName(v[i]), "L.return.") && !named.count(labelName(v[i]))))) continue;
        out.push_back(v[i]);
    }
    v.swap(out);
}

// **A short arm is predicated instead of branched over**: `[P] B L; i1..ik; B M; L:` with k at most four
// plain instructions none of which writes P or a stack pointer becomes `[!P] i1..ik; [!P] B M; L:` - the
// arm skipped by its predicate where the branch skipped it, and the branch that skipped it gone; k may be 0.
bool predicateArms(std::vector<Line> &v) {
    std::set<std::string> labels;
    for (std::size_t i = 0; i < v.size(); i++) if (isLabel(v[i])) labels.insert(labelName(v[i]));
    bool changed = false;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &b = v[i];
        if (!b.instr || b.mnem != "B" || b.pred.empty() || !labels.count(b.ops[0])) continue;
        const std::string P = b.pred[0] == '!' ? b.pred.substr(1) : b.pred;
        const std::string sense = b.pred[0] == '!' ? P : "!" + P;
        std::size_t j = i + 1;
        bool ok = true;
        for (; j < v.size() && v[j].instr && !isBranch(v[j].mnem) && ok; j++) {
            const Line &l = v[j];
            if (j - i > 4 || !l.pred.empty() || (!isLoad(l.mnem) && !isStore(l.mnem) && unitsFor(l) == 0)) ok = false;
            std::vector<std::string> reads, writes;
            readsAndWrites(l, reads, writes);
            if (has(writes, P) || has(reads, "B15") || has(writes, "B15") || has(writes, "A15")) ok = false;
        }
        if (!ok || j >= v.size() || !v[j].instr || v[j].mnem != "B" || !v[j].pred.empty() || !labels.count(v[j].ops[0])) continue;
        std::size_t m = j + 1;
        bool target = false;
        for (; m < v.size() && isLabel(v[m]); m++) if (labelName(v[m]) == b.ops[0]) target = true;
        if (!target) continue;
        for (std::size_t k = i + 1; k <= j; k++) v[k] = rebuilt(v[k].mnem, v[k].ops, sense);
        v.erase(v.begin() + static_cast<long>(i));
        i--;
        changed = true;
    }
    return changed;
}

// **A value already in a register is not made again**: within a straight run - a block and those it falls into
// that no branch names - each result is numbered by its operation and its sources' numbers, a load's by its
// address and the store count; one that would remake a number a register holds becomes a copy. `a[j]` read twice.
void numberValues(std::vector<Line> &v) {
    std::set<std::string> named;
    for (std::size_t i = 0; i < v.size(); i++)
        if (v[i].instr && v[i].mnem == "B") named.insert(v[i].ops[0]);
    std::map<std::string, long> val;            // register -> number
    std::map<std::string, long> made;           // key -> the number it makes
    std::map<long, std::size_t> defAt;          // number -> the line that made it, this run
    long next = 1, stores = 0;
    std::size_t runStart = 0;
    auto reset = [&](std::size_t at) { val.clear(); made.clear(); defAt.clear(); next++; stores++; runStart = at; };
    // The number's register was overwritten before the remake: the line that made it is given a fresh
    // register - one dead there and untouched up to here - its readers up to the next write of the old
    // register renamed with it, and the remake becomes a copy. `a[j]` read twice with `a[j]` in the way.
    auto renameFirst = [&](std::size_t p, std::size_t here, const std::string &D) -> std::string {
        std::size_t end = here;                 // the web: D's readers before its next write, up to here
        for (std::size_t k = p + 1; k < here; k++) {
            std::vector<std::string> r, w;
            readsAndWrites(v[k], r, w);
            if (has(w, D) && v[k].pred.empty()) { end = k; break; }
        }
        for (int n = 16; n < 32; n++) {
            const std::string F = "A" + std::to_string(n);
            bool ok = deadAfter(v, p, F);
            for (std::size_t k = p + 1; k <= here && ok; k++) {
                std::vector<std::string> r, w;
                readsAndWrites(v[k], r, w);
                if (has(r, F) || has(w, F)) ok = false;
            }
            if (!ok) continue;
            std::vector<Line> changed;
            std::vector<std::size_t> where;
            Line d = v[p];
            d.ops.back() = F;
            d = rebuilt(d.mnem, d.ops, d.pred);
            if (crossings(d) > 1 || (unitsFor(d) == 0 && !isLoad(d.mnem))) continue;
            changed.push_back(d); where.push_back(p);
            for (std::size_t k = p + 1; k <= end && k < here && ok; k++) {   // the web's last line may write D too
                std::vector<std::string> r, w;
                readsAndWrites(v[k], r, w);
                if (!has(r, D)) continue;
                Line u = v[k];
                if (!u.pred.empty() || u.mnem == "MVKH" || u.mnem == "ADDK" || (has(w, D) && k != end)) { ok = false; break; }
                for (std::size_t o = 0; o + 1 < u.ops.size() || (o < u.ops.size() && (isStore(u.mnem) || u.ops[o][0] == '*')); o++) {
                    if (u.ops[o].find(':') != std::string::npos) { ok = false; break; }
                    if (u.ops[o][0] == '*' && sideOf(D) != 'A' && renamed(u.ops[o], D, F) != u.ops[o]) { ok = false; break; }
                    u.ops[o] = renamed(u.ops[o], D, F);
                }
                if (!ok) break;
                Line r2 = rebuilt(u.mnem, u.ops, u.pred);
                if (crossings(r2) > 1 || (unitsFor(r2) == 0 && !isLoad(r2.mnem) && !isStore(r2.mnem))) { ok = false; break; }
                changed.push_back(r2); where.push_back(k);
            }
            if (!ok) continue;
            for (std::size_t c = 0; c < changed.size(); c++) v[where[c]] = changed[c];
            return F;
        }
        return std::string();
    };
    auto numberOf = [&](const std::string &r) -> long { std::map<std::string, long>::iterator it = val.find(r); if (it == val.end()) { val[r] = next++; return val[r]; } return it->second; };
    auto keyOf = [&](const std::string &op) -> std::string {   // an operand with its registers' numbers
        std::string out;
        for (std::size_t k = 0; k < op.size();) {
            if ((op[k] == 'A' || op[k] == 'B') && k + 1 < op.size() && std::isdigit(static_cast<unsigned char>(op[k + 1])) && (k == 0 || !isNameChar(op[k - 1]))) {
                std::size_t e = k + 1;
                while (e < op.size() && std::isdigit(static_cast<unsigned char>(op[e]))) e++;
                out += "#" + std::to_string(numberOf(op.substr(k, e - k)));
                k = e;
            } else out += op[k++];
        }
        return out;
    };
    for (std::size_t i = 0; i < v.size(); i++) {
        Line &l = v[i];
        if (isLabel(l)) { if (named.count(labelName(l))) reset(i); continue; }
        if (!l.instr) { reset(i); continue; }
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        if (isBranch(l.mnem)) { if (l.pred.empty() || l.mnem == "CALLP" || sideOf(l.ops[0])) reset(i); continue; }
        bool pair = false, incr = false;
        for (std::size_t o = 0; o < l.ops.size(); o++) {
            if (l.ops[o].find(':') != std::string::npos) pair = true;
            if (l.ops[o][0] == '*' && (l.ops[o].find("++") != std::string::npos || l.ops[o].find("--") != std::string::npos)) incr = true;
        }
        const bool plain = l.pred.empty() && !pair && !incr && writes.size() == 1 && !isStore(l.mnem) && l.ops.size() >= 2 &&
                           writes[0] != "A15" && writes[0] != "B15" && writes[0] != "B14" && writes[0] != "B3";
        if (plain) {
            std::string key = l.mnem;
            for (std::size_t o = 0; o + 1 < l.ops.size(); o++) key += " " + keyOf(l.ops[o]);
            if (l.mnem == "MVKH" || l.mnem == "ADDK") key += " #" + std::to_string(numberOf(writes[0]));
            if (isLoad(l.mnem)) key += " @" + std::to_string(stores);
            const std::string dst = writes[0];
            if (l.mnem == "MV" && sideOf(l.ops[0])) {                           // a copy carries the number
                const long n = numberOf(l.ops[0]);
                if (val.count(dst) && val[dst] == n) { l = parse(""); continue; }   // a copy back of what it holds
                val[dst] = n;
                continue;
            }
            std::map<std::string, long>::iterator it = made.find(key);
            const long number = it != made.end() ? it->second : next++;   // the same operation on the same sources
            if (it == made.end()) made[key] = number;
            std::string holder;                 // a register still holding that number, the destination itself first
            if (val.count(dst) && val[dst] == number) holder = dst;
            for (std::map<std::string, long>::const_iterator h = val.begin(); h != val.end() && holder.empty(); ++h)
                if (h->second == number && sideOf(h->first) && h->first != "A15" && h->first != "B15") holder = h->first;
            if (holder.empty() && it != made.end() && defAt.count(number) && defAt[number] >= runStart && !isStore(l.mnem)) {
                const std::size_t p = defAt[number];
                std::vector<std::string> r0, w0;
                readsAndWrites(v[p], r0, w0);
                if (v[p].mnem != "MVKL" && v[p].mnem != "MVKH" && v[p].mnem != "ADDK" && sideOf(w0[0]) && w0[0] != "A15" && w0[0] != "B15") {
                    holder = renameFirst(p, i, w0[0]);
                    if (!holder.empty()) val[holder] = number;
                }
            }
            if (!holder.empty()) {
                if (holder == dst) { l = parse(""); continue; }   // gone: no operands left to read
                Line r = make("MV", holder, dst);
                if (crossings(r) <= 1) { l = r; val[dst] = number; continue; }
            }
            val[dst] = number;
            defAt[number] = i;
            continue;
        }
        for (std::size_t w = 0; w < writes.size(); w++) val[writes[w]] = next++;
        if (isStore(l.mnem)) stores++;
    }
    std::vector<Line> out;
    for (std::size_t i = 0; i < v.size(); i++) if (v[i].instr || !v[i].raw.empty()) out.push_back(v[i]);
    v.swap(out);
}

// **A loop's back edge takes the test with it**: `B L` where L's block is a test ending in `[P] B M` and
// M is the label after the jump becomes a copy of the test ending in `[!P] B L$rot`, a label placed after
// the head's own branch - one branch a turn instead of two, and the test overlapping the step.
void rotateLoops(std::vector<Line> &v) {
    std::map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < v.size(); i++) if (isLabel(v[i])) at[labelName(v[i])] = i;
    std::vector<Line> out;
    std::set<std::string> rotated;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &b = v[i];
        bool taken = false;
        if (b.instr && b.mnem == "B" && b.pred.empty() && at.count(b.ops[0]) && i + 1 < v.size() && isLabel(v[i + 1])) {
            std::size_t h = at[b.ops[0]], e = h + 1;
            while (e < v.size() && v[e].instr && !isBranch(v[e].mnem)) e++;
            if (e < v.size() && e - h <= 17 && v[e].instr && v[e].mnem == "B" && !v[e].pred.empty() &&
                v[e].ops[0] == labelName(v[i + 1]) && (i < h || i > e)) {
                for (std::size_t k = h + 1; k < e; k++) out.push_back(v[k]);
                const std::string &p = v[e].pred;
                out.push_back(rebuilt("B", std::vector<std::string>(1, b.ops[0] + "$rot"), p[0] == '!' ? p.substr(1) : "!" + p));
                rotated.insert(b.ops[0]);
                taken = true;
            }
        }
        if (!taken) out.push_back(b);
    }
    v.swap(out);
    for (std::size_t i = 0; i < v.size(); i++) {
        if (!isLabel(v[i]) || !rotated.count(labelName(v[i]))) continue;
        std::size_t e = i + 1;
        while (v[e].mnem != "B") e++;
        v.insert(v.begin() + static_cast<long>(e) + 1, parse(labelName(v[i]) + "$rot:"));
    }
}

// **A byte read for the test is not read again for the work**: a rotated loop whose block opens with `LDx *R, X`
// and ends - after R's last write - with the same `LDx *R, X` that its test reads, entered from a block whose
// last write of X is that load too, opens without the load: on either path X holds `*R` already. `for (; *p; ++p)`.
void reuseEntryLoads(std::vector<Line> &v) {
    std::map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < v.size(); i++) if (isLabel(v[i])) at[labelName(v[i])] = i;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &b = v[i];
        if (!b.instr || b.mnem != "B" || b.pred.empty() || !at.count(b.ops[0])) continue;
        const std::size_t h = at[b.ops[0]];
        if (h >= i || h + 1 >= v.size()) continue;
        const Line &top = v[h + 1];
        if (!top.instr || !isLoad(top.mnem) || top.ops.size() != 2 || top.ops[0][0] != '*' || !top.pred.empty()) continue;
        std::vector<std::string> addr;
        registersIn(top.ops[0], addr);
        if (addr.size() != 1 || top.ops[0].find('+') != std::string::npos || top.ops[0].find('-') != std::string::npos) continue;
        const std::string R = addr[0], X = top.ops[1];
        if (X.find(':') != std::string::npos) continue;
        // One block: no label between; the same load again after R's last write, nothing after it touching R, X or memory.
        std::size_t last = 0;
        bool ok = true;
        for (std::size_t k = h + 2; k < i && ok; k++) {
            if (!v[k].instr || isBranch(v[k].mnem)) { ok = false; break; }
            if (v[k].raw == top.raw) { last = k; continue; }
            std::vector<std::string> r, w;
            readsAndWrites(v[k], r, w);
            if (last != 0 && (has(w, R) || has(w, X) || isStore(v[k].mnem))) last = 0;
        }
        if (!ok || last == 0) continue;
        // The entry: back from the head over one block's fall-through, the last write of X the same load, memory untouched.
        bool entry = false;
        for (std::size_t k = h; k-- > 0;) {
            if (isLabel(v[k]) || !v[k].instr || (isBranch(v[k].mnem) && v[k].pred.empty())) break;
            if (v[k].raw == top.raw) { entry = true; break; }
            std::vector<std::string> r, w;
            readsAndWrites(v[k], r, w);
            if (has(w, R) || has(w, X) || isStore(v[k].mnem) || v[k].mnem == "CALLP") break;
        }
        if (!entry) continue;
        v[h + 1] = parse("");
    }
    std::vector<Line> out;
    for (std::size_t i = 0; i < v.size(); i++) if (v[i].instr || !v[i].raw.empty()) out.push_back(v[i]);
    v.swap(out);
}

// One loop's rewrite applied before the next loop is looked at, so liveness asked of the text is of the text: the lines
// `pre` before line h, the lines blanked dropped; where the back edge at i went.
std::size_t applyLoop(std::vector<Line> &v, std::size_t h, const std::vector<Line> &pre, std::size_t i) {
    std::vector<Line> out;
    std::size_t at = i;
    for (std::size_t k = 0; k < v.size(); k++) {
        if (k == h) out.insert(out.end(), pre.begin(), pre.end());
        if (v[k].instr || !v[k].raw.empty()) out.push_back(v[k]);
        if (k == i) at = out.size() - 1;
    }
    v.swap(out);
    return at;
}

// **A constant made inside a loop nest is made before it**: `MVKL sym, d; MVKH sym, d`, the only writer of d
// anywhere in a loop's region - inner loops and all - with d not live into the head and no branch into
// the region but the back edge to its head, goes in front of the head. `comp` in sieve's outer loop.
void hoistConstantPairs(std::vector<Line> &v) {
    computeLiveness(v);
    std::map<std::string, std::size_t> at;
    std::map<std::string, int> named;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) at[labelName(v[i])] = i;
        if (v[i].instr) for (std::size_t o = 0; o < v[i].ops.size(); o++) named[v[i].ops[o]]++;
    }
    std::set<std::string> hoistedHeads;                    // a region holding one of these has been rewritten already
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &b = v[i];
        if (!b.instr || b.mnem != "B" || !at.count(b.ops[0]) || named[b.ops[0]] != 1) continue;
        const std::size_t h = at[b.ops[0]];
        if (h >= i || h == 0 || !v[h - 1].instr || v[h - 1].mnem != "B" || v[h - 1].pred.empty()) continue;
        bool ok = true;                                     // every branch into the region starts inside it
        std::set<std::string> inside;
        for (std::size_t k = h; k <= i; k++) if (isLabel(v[k])) inside.insert(labelName(v[k]));
        for (std::size_t k = 0; k < v.size() && ok; k++)
            if ((k < h || k > i) && v[k].instr && v[k].mnem == "B" && inside.count(v[k].ops[0])) ok = false;
        for (std::size_t k = h + 1; k < i && ok; k++) if ((!v[k].instr && !isLabel(v[k])) || (isLabel(v[k]) && hoistedHeads.count(labelName(v[k])))) ok = false;
        if (!ok) continue;
        const std::uint64_t headIn = liveInAt(v, h);
        std::map<std::string, int> writers;
        for (std::size_t k = h + 1; k < i; k++) {
            if (!v[k].instr) continue;
            std::vector<std::string> r, w;
            readsAndWrites(v[k], r, w);
            if (v[k].mnem == "CALLP" || (v[k].mnem == "B" && !inside.count(v[k].ops[0]) && !at.count(v[k].ops[0]))) { writers["*"]++; }
            for (std::size_t x = 0; x < w.size(); x++) writers[w[x]]++;
        }
        if (writers.count("*")) continue;                   // a call clobbers what a constant would sit in
        std::set<std::string> used;
        for (std::size_t k = h; k <= i; k++) if (v[k].instr) { std::vector<std::string> r, w; readsAndWrites(v[k], r, w); used.insert(r.begin(), r.end()); used.insert(w.begin(), w.end()); }
        std::vector<Line> hoisted;
        for (std::size_t k = h + 1; k + 1 < i; k++) {
            const Line &l = v[k], &m = v[k + 1];
            if (!l.instr || l.mnem != "MVKL" || l.ops.size() != 2 || !l.pred.empty() || !m.instr || m.mnem != "MVKH" || m.ops != l.ops || !m.pred.empty()) continue;
            const std::string d = l.ops[1];
            if (!sideOf(d) || d == "A15" || d == "B15" || d == "B3" || d == "B14") continue;
            std::string f = d;
            std::vector<std::pair<std::size_t, Line> > web;         // the readers renamed, where d is written elsewhere too
            if (writers[d] != 2 || (headIn & bitOf(d))) {
                // A fresh register the region never names and the head is not entered with: the pair's value in
                // it, its readers up to d's next write in this straight run renamed, or the pair stays.
                f.clear();
                for (int side = 0; side < 2 && f.empty(); side++)
                    for (int n = 16; n < 32 && f.empty(); n++) {
                        const std::string c = std::string(sideOf(d) == 'A' ? (side ? "B" : "A") : (side ? "A" : "B")) + std::to_string(n);
                        if (!used.count(c) && !(headIn & bitOf(c))) f = c;
                    }
                if (f.empty()) continue;
                bool ok = true, ended = false;
                for (std::size_t x = k + 2; x < i && ok && !ended; x++) {
                    const Line &u = v[x];
                    if (!u.instr || isBranch(u.mnem)) { ok = false; break; }
                    std::vector<std::string> r, w;
                    readsAndWrites(u, r, w);
                    if (has(w, d)) ended = true;
                    if (!has(r, d)) continue;
                    if (!u.pred.empty() || u.mnem == "MVKH" || u.mnem == "ADDK") { ok = false; break; }
                    std::vector<std::string> ops = u.ops;
                    for (std::size_t o = 0; o < ops.size(); o++) {
                        if (ops[o].find(':') != std::string::npos) { ok = false; break; }
                        const bool read = o + 1 < ops.size() || isStore(u.mnem) || ops[o][0] == '*';
                        if (read && !(o + 1 == ops.size() && !isStore(u.mnem) && ops[o][0] != '*')) {
                            if (ops[o][0] == '*' && sideOf(f) != sideOf(d) && renamed(ops[o], d, f) != ops[o]) { ok = false; break; }
                            ops[o] = renamed(ops[o], d, f);
                        }
                    }
                    if (!ok) break;
                    Line r2 = rebuilt(u.mnem, ops, u.pred);
                    if (crossings(r2) > 1 || (unitsFor(r2) == 0 && !isLoad(r2.mnem) && !isStore(r2.mnem))) { ok = false; break; }
                    web.push_back(std::make_pair(x, r2));
                }
                if (!ok || !ended) continue;
            }
            Line a = l, b2 = m;
            a.ops[1] = f; b2.ops[1] = f;
            hoisted.push_back(rebuilt(a.mnem, a.ops));
            hoisted.push_back(rebuilt(b2.mnem, b2.ops));
            for (std::size_t x = 0; x < web.size(); x++) v[web[x].first] = web[x].second;
            used.insert(f);
            v[k] = parse("");
            v[k + 1] = parse("");
            k++;
        }
        if (hoisted.empty()) continue;
        hoistedHeads.insert(b.ops[0]);
        i = applyLoop(v, h, hoisted, i);
        at.clear();
        for (std::size_t k = 0; k < v.size(); k++) if (isLabel(v[k])) at[labelName(v[k])] = k;
    }
}

// **A loop's invariants move to its preheader**: in a rotated loop - one block, entered by falling into its
// head from the test that guards it - an instruction whose sources the loop never writes is computed once
// before the head; a destination the loop also writes elsewhere takes a fresh register for that one value.
void hoistInvariants(std::vector<Line> &v) {
    computeLiveness(v);
    std::set<std::string> labels, named;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) labels.insert(labelName(v[i]));
        for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    }
    for (std::size_t i = 0; i < v.size(); i++) {
        if (!v[i].instr || v[i].mnem != "B" || v[i].pred.empty() || !labels.count(v[i].ops[0])) continue;
        std::size_t h = i; int uses = 0;
        for (std::size_t k = 0; k < i; k++) if (isLabel(v[k]) && labelName(v[k]) == v[i].ops[0]) h = k;
        for (std::size_t k = 0; k < v.size(); k++) if (v[k].instr) for (std::size_t o = 0; o < v[k].ops.size(); o++) if (v[k].ops[o] == v[i].ops[0]) uses++;
        if (h == i || uses != 1 || h == 0 || !v[h - 1].instr || v[h - 1].mnem != "B" || v[h - 1].pred.empty()) continue;
        // The body may leave by a conditional branch to a label outside it - an exit - and by nothing else.
        bool ok = true;
        std::vector<std::size_t> body, exits;
        std::set<std::string> inside;
        for (std::size_t k = h; k <= i; k++) if (isLabel(v[k])) inside.insert(labelName(v[k]));
        for (std::size_t k = h + 1; k < i && ok; k++) {
            if (isLabel(v[k])) { ok = passThrough(v[k], named); continue; }
            if (!v[k].instr) ok = false;
            else if (isBranch(v[k].mnem)) { if (v[k].mnem != "B" || v[k].pred.empty() || inside.count(v[k].ops[0]) || !labels.count(v[k].ops[0])) ok = false; else exits.push_back(k); }
            else body.push_back(k);
        }
        if (!ok) continue;
        // The liveness of the loop as it is entered: its own edits rename what they hoist, never what is asked about after.
        const std::uint64_t backOut = liveOutAt(v, i);
        std::vector<std::uint64_t> exitOut(exits.size());
        std::uint64_t liveAtExits = backOut;
        for (std::size_t e = 0; e < exits.size(); e++) liveAtExits |= exitOut[e] = liveOutAt(v, exits[e]);
        std::set<std::string> used;
        for (std::size_t b = 0; b < body.size(); b++) {
            std::vector<std::string> reads, writes;
            readsAndWrites(v[body[b]], reads, writes);
            for (std::size_t r = 0; r < reads.size(); r++) used.insert(reads[r]);
            for (std::size_t w = 0; w < writes.size(); w++) used.insert(writes[w]);
        }
        std::vector<std::string> pool;
        for (int side = 0; side < 2; side++)
            for (int k = 3; k < 32; k++) {
                if ((k >= 10 && k <= 15) || (side == 1 && k == 3)) continue;
                std::string r = std::string(side ? "B" : "A") + std::to_string(k);
                if (!used.count(r) && !(liveAtExits & bitOf(r))) pool.push_back(r);
            }
        std::vector<Line> hoisted;
        std::vector<bool> gone(body.size(), false);
        for (bool changed = true; changed;) {
            changed = false;
            std::map<std::string, int> writers;                 // the loop's writes per register, this round
            for (std::size_t b = 0; b < body.size(); b++) {
                if (gone[b]) continue;
                std::vector<std::string> reads, writes;
                readsAndWrites(v[body[b]], reads, writes);
                for (std::size_t w = 0; w < writes.size(); w++) writers[writes[w]]++;
            }
            for (std::size_t b = 0; b < body.size() && !changed; b++) {
                const Line &l = v[body[b]];
                if (gone[b] || !l.pred.empty() || isLoad(l.mnem) || isStore(l.mnem) || l.mnem == "MVKH" || l.mnem == "ADDK" || l.ops.size() < 2) continue;
                std::vector<std::string> reads, writes;
                readsAndWrites(l, reads, writes);
                if (writes.size() != 1) continue;
                const std::string d = writes[0];
                if (d == "A15" || d == "B15" || d == "B3" || d == "B14" || has(reads, d)) continue;
                bool pair = l.mnem == "MVKL" && b + 1 < body.size() && !gone[b + 1] && v[body[b + 1]].mnem == "MVKH" && v[body[b + 1]].ops == l.ops && v[body[b + 1]].pred.empty();
                if (l.mnem == "MVKL" && !pair) continue;
                bool invariant = true;
                for (std::size_t r = 0; r < reads.size(); r++) if (writers.count(reads[r])) invariant = false;
                if (!invariant) continue;
                // The value's web: its reads up to the next write of d; none may come before the loop's first write.
                std::size_t next = body.size(), first = b;
                for (std::size_t k = 0; k < body.size(); k++) {
                    if (gone[k] || k == b || (pair && k == b + 1)) continue;
                    std::vector<std::string> r2, w2;
                    readsAndWrites(v[body[k]], r2, w2);
                    if (has(w2, d)) { if (k < first) first = k; if (k > b && k < next) next = k; }
                }
                bool early = false;
                for (std::size_t k = 0; k < first; k++) { std::vector<std::string> r2, w2; readsAndWrites(v[body[k]], r2, w2); if (has(r2, d)) early = true; }
                if (early && next == body.size()) continue;                        // this value reaches the next turn
                if (next < body.size() && (v[body[next]].mnem == "MVKH" || v[body[next]].mnem == "ADDK")) continue;
                // The web renamed to f: every reader rebuilt, the next writer's sources too; false where a
                // reader cannot take f - a memory operand or a predicate, or a crossing the form lacks.
                auto renameWeb = [&](const std::string &f, bool apply) -> bool {
                    Line own = l; own.ops.back() = f;                          // the hoisted instruction's own form
                    if (crossings(rebuilt(own.mnem, own.ops)) > 1 || unitsFor(rebuilt(own.mnem, own.ops)) == 0) return false;
                    for (std::size_t k = b + 1; k <= next && k < body.size(); k++) {
                        if (gone[k] || (pair && k == b + 1)) continue;
                        Line &u = v[body[k]];
                        std::vector<std::string> ops = u.ops;
                        std::size_t last = isStore(u.mnem) || u.mnem == "B" || ops.back()[0] == '*' ? ops.size() : ops.size() - 1;
                        for (std::size_t o = 0; o < ops.size(); o++)
                            if ((o < last || ops[o][0] == '*' || (k != next && (u.mnem == "MVKH" || u.mnem == "ADDK"))) && renamed(ops[o], d, f) != ops[o]) {
                                if ((sideOf(f) != sideOf(d) && ops[o][0] == '*') || ops[o].find(':') != std::string::npos) return false;
                                ops[o] = renamed(ops[o], d, f);
                            }
                        if (!u.pred.empty() && renamed(u.pred, d, f) != u.pred) return false;
                        Line r = rebuilt(u.mnem, ops, u.pred);
                        if (sideOf(f) != sideOf(d) && (crossings(r) > 1 || unitsFor(r) == 0)) return false;
                        if (apply) u = r;
                    }
                    return true;
                };
                // The same value hoisted already - the same instruction over the same sources - is reused.
                std::string f;
                std::uint64_t liveAfter = backOut, liveBefore = 0;   // d at the exits past this write, and before it
                for (std::size_t e = 0; e < exits.size(); e++) (exits[e] > body[b] ? liveAfter : liveBefore) |= exitOut[e];
                if (liveBefore & bitOf(d)) continue;                       // an exit before the write reads the value coming in
                const bool exits = next == body.size() && (liveAfter & bitOf(d));
                if (!exits) for (std::size_t e = 0; e < hoisted.size() && f.empty(); e++) {
                    const Line &m = hoisted[e];
                    if (m.mnem != l.mnem || m.ops.size() != l.ops.size()) continue;
                    bool same = true;
                    for (std::size_t o = 0; o + 1 < l.ops.size(); o++) if (m.ops[o] != l.ops[o]) same = false;
                    if (same && pair && !(e + 1 < hoisted.size() && hoisted[e + 1].mnem == "MVKH" && hoisted[e + 1].ops == m.ops)) same = false;
                    if (same && renameWeb(m.ops.back(), false)) f = m.ops.back();
                }
                const bool reuse = !f.empty();
                if (!reuse) {
                    if (writers[d] <= (pair ? 2 : 1)) f = d;
                    else {
                        if (exits) continue;                                       // the exit reads this value
                        for (int side = 1; side >= 0 && f.empty(); side--)
                            for (std::size_t k = 0; k < pool.size() && f.empty(); k++)
                                if (sideIndex(pool[k]) == side && renameWeb(pool[k], false)) { f = pool[k]; pool.erase(pool.begin() + static_cast<long>(k)); }
                        if (f.empty()) continue;
                    }
                }
                if (f != d) renameWeb(f, true);
                if (reuse) { for (std::size_t k = b; k < b + (pair ? 2u : 1u); k++) gone[k] = true; changed = true; continue; }
                for (std::size_t k = b; k < b + (pair ? 2u : 1u); k++) {
                    Line m = v[body[k]];
                    m.ops.back() = f;
                    hoisted.push_back(rebuilt(m.mnem, m.ops));
                    gone[k] = true;
                }
                changed = true;
            }
        }
        if (hoisted.empty()) continue;
        for (std::size_t b = 0; b < body.size(); b++) if (gone[b]) v[body[b]] = parse("");
        i = applyLoop(v, h, hoisted, i);
    }
}

// **An address stepped with the counter is a pointer the access steps itself**: in a rotated loop whose counter iv is stepped
// once by `ADD 1, iv, iv`, `SHL iv, k, S; ADD base, S, T; LDx/STx *T` - or `ADD base, iv, T` - with base never written in the
// loop and S, T read nowhere else, is `*P++(1<<k)`, P made once in the preheader: T where nothing else writes or exits with it.
void inductionPointers(std::vector<Line> &v) {
    computeLiveness(v);
    std::set<std::string> labels, named;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) labels.insert(labelName(v[i]));
        for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    }
    for (std::size_t i = 0; i < v.size(); i++) {
        if (!v[i].instr || v[i].mnem != "B" || v[i].pred.empty() || !labels.count(v[i].ops[0])) continue;
        std::size_t h = i; int uses = 0;
        for (std::size_t k = 0; k < i; k++) if (isLabel(v[k]) && labelName(v[k]) == v[i].ops[0]) h = k;
        for (std::size_t k = 0; k < v.size(); k++) if (v[k].instr) for (std::size_t o = 0; o < v[k].ops.size(); o++) if (v[k].ops[o] == v[i].ops[0]) uses++;
        if (h == i || uses != 1 || h == 0) continue;
        // Entered by falling in from the guard's branch over a straight run - the preheader, where the pointers are made.
        std::size_t g = h;
        while (g > 0 && v[g - 1].instr && !isBranch(v[g - 1].mnem)) g--;
        if (g == 0 || !v[g - 1].instr || v[g - 1].mnem != "B" || v[g - 1].pred.empty()) continue;
        bool ok = true;
        std::vector<std::size_t> body, exits;
        std::set<std::string> inside;
        for (std::size_t k = h; k <= i; k++) if (isLabel(v[k])) inside.insert(labelName(v[k]));
        for (std::size_t k = h + 1; k < i && ok; k++) {
            if (isLabel(v[k])) { ok = passThrough(v[k], named); continue; }
            if (!v[k].instr) ok = false;
            else if (isBranch(v[k].mnem)) { if (v[k].mnem != "B" || v[k].pred.empty() || inside.count(v[k].ops[0]) || !labels.count(v[k].ops[0])) ok = false; else exits.push_back(k); }
            else body.push_back(k);
        }
        if (!ok) continue;
        // The liveness of the loop as it is entered: its own edits step pointers it chose from registers free here.
        const std::uint64_t headIn = liveInAt(v, h);
        std::uint64_t liveAtExits = liveOutAt(v, i);
        for (std::size_t e = 0; e < exits.size(); e++) liveAtExits |= liveOutAt(v, exits[e]);
        std::map<std::string, int> writers, readers;
        std::vector<std::vector<std::string> > reads(body.size()), writes(body.size());
        std::set<std::string> used;
        for (std::size_t b = 0; b < body.size(); b++) {
            readsAndWrites(v[body[b]], reads[b], writes[b]);
            for (std::size_t r = 0; r < reads[b].size(); r++) { readers[reads[b][r]]++; used.insert(reads[b][r]); }
            for (std::size_t w = 0; w < writes[b].size(); w++) { writers[writes[b][w]]++; used.insert(writes[b][w]); }
        }
        // The counter: stepped once, by one.
        std::string iv; std::size_t stepAt = 0;
        for (std::size_t b = 0; b < body.size(); b++) {
            const Line &l = v[body[b]];
            if (l.mnem != "ADD" || !l.pred.empty() || l.ops.size() != 3 || writers[l.ops[2]] != 1) continue;
            if ((l.ops[0] == "1" && l.ops[1] == l.ops[2]) || (l.ops[1] == "1" && l.ops[0] == l.ops[2])) { if (!iv.empty()) { iv.clear(); break; } iv = l.ops[2]; stepAt = b; }
        }
        if (iv.empty()) continue;
        std::vector<std::string> pool;
        for (int side = 0; side < 2; side++)
            for (int k = 3; k < 32; k++) {
                if ((k >= 10 && k <= 15) || (side == 1 && k == 3)) continue;
                std::string r = std::string(side ? "B" : "A") + std::to_string(k);
                if (!used.count(r) && !(liveAtExits & bitOf(r)) && !(headIn & bitOf(r))) pool.push_back(r);
            }
        // The last write of r before b, with no read of r between it and b but at b itself; body.size() for none.
        auto reaches = [&](const std::string &r, std::size_t b) -> std::size_t {
            for (std::size_t k = b; k-- > 0;) {
                if (has(writes[k], r)) return k;
                if (has(reads[k], r)) return body.size();
            }
            return body.size();
        };
        // Whether the value r holds at b is read again after b, before the loop writes r anew.
        auto readAgain = [&](const std::string &r, std::size_t b) -> bool {
            for (std::size_t k = b + 1; k < body.size(); k++) {
                if (has(reads[k], r)) return true;
                if (has(writes[k], r)) return false;
            }
            return (liveAtExits & bitOf(r)) != 0 || (headIn & bitOf(r)) != 0;
        };
        std::vector<Line> entry;
        std::set<std::size_t> gone;
        for (std::size_t b = 0; b < body.size(); b++) {
            const Line &m = v[body[b]];
            if (!m.pred.empty() || m.ops.size() != 2 || (!isLoad(m.mnem) && !isStore(m.mnem))) continue;
            const std::string &addr = isStore(m.mnem) ? m.ops[1] : m.ops[0];
            if (addr.size() < 2 || addr[0] != '*' || !sideOf(addr.substr(1))) continue;
            const std::string T = addr.substr(1);
            std::vector<std::string> data;
            registersIn(m.ops[isStore(m.mnem) ? 0 : 1], data);
            if (has(data, T) || T == "A15" || T == "B15" || T == iv || readAgain(T, b)) continue;
            const std::size_t ta = reaches(T, b);
            if (ta >= body.size() || gone.count(ta)) continue;
            const Line &add = v[body[ta]];
            if (add.mnem != "ADD" || !add.pred.empty() || add.ops.size() != 3 || isNumber(add.ops[0]) || isNumber(add.ops[1])) continue;
            // One source is the counter, or a shift of it reaching here alone; the other the base, which the loop never writes.
            std::string S, base; long k = 0; std::size_t sa = ta; bool found = false;
            for (int side = 0; side < 2 && !found; side++) {
                const std::string &cand = add.ops[static_cast<std::size_t>(side)], &other = add.ops[static_cast<std::size_t>(1 - side)];
                if (!sideOf(cand) || !sideOf(other) || writers.count(other) || cand == other || other == iv) continue;
                if (cand == iv) { base = other; found = true; break; }
                const std::size_t sh = reaches(cand, ta);
                if (sh >= body.size() || gone.count(sh) || (cand != T && readAgain(cand, ta))) continue;   // `ADD base, S, S` reads S once more, as T
                const Line &shl = v[body[sh]];
                if (shl.mnem == "SHL" && shl.pred.empty() && shl.ops.size() == 3 && shl.ops[0] == iv && isNumber(shl.ops[1]) && cand != iv) {
                    S = cand; k = std::atol(shl.ops[1].c_str()); sa = sh; base = other; found = true;
                }
            }
            if (!found || k < 0 || k > 3 || base == T) continue;
            const long size = accessSize(m.mnem), stride = 1L << k;
            if (stride % size != 0 || stride / size > 31) continue;
            // The pointer: T where this is its only write and no exit reads it, else a register the loop never names.
            std::string P = T;
            if (writers[T] != 1 || (liveAtExits & bitOf(T)) || (headIn & bitOf(T))) {
                P.clear();
                for (std::size_t f = 0; f < pool.size() && P.empty(); f++) if (sideOf(pool[f]) == sideOf(T)) { P = pool[f]; pool.erase(pool.begin() + static_cast<long>(f)); }
                if (P.empty()) continue;
            }
            // The address the first turn uses: base + (iv << k), one more stride where the shift reads the stepped counter.
            const bool afterStep = stepAt < sa;
            Line first = S.empty() ? make("ADD", base, iv, P) : make("SHL", iv, std::to_string(k), P);
            if (!legalForm(first)) continue;
            Line second = make("ADD", base, P, P);
            if (!S.empty() && !legalForm(second)) continue;
            entry.push_back(first);
            if (!S.empty()) entry.push_back(second);
            if (afterStep) entry.push_back(make("ADD", std::to_string(stride), P, P));
            std::vector<std::string> ops = m.ops;
            ops[isStore(m.mnem) ? 1 : 0] = "*" + P + "++(" + std::to_string(stride) + ")";
            if (std::getenv("CPP11_INDUCT")) std::fprintf(stderr, "induct%s -> P=%s stride=%ld base=%s S=%s after=%d\n", m.raw.c_str(), P.c_str(), stride, base.c_str(), S.c_str(), afterStep ? 1 : 0);
            v[body[b]] = rebuilt(m.mnem, ops);
            gone.insert(ta);
            if (!S.empty()) gone.insert(sa);
        }
        if (entry.empty()) continue;
        for (std::set<std::size_t>::const_iterator it = gone.begin(); it != gone.end(); ++it) v[body[*it]] = parse("");
        i = applyLoop(v, h, entry, i);
    }
}

// **A loop that fills memory with one value stores doublewords**: a rotated loop of `ADD 1, iv, iv`, `STB|STH|STW V,
// *P++(k)` and the compare of iv against a bound the loop never writes - K elements, known at entry - becomes, past 15
// elements, a loop up to the next 8-byte boundary, `STDW` of the value replicated, and the elements left over.
void widenFills(std::vector<Line> &v) {
    computeLiveness(v);
    std::set<std::string> labels, named;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) labels.insert(labelName(v[i]));
        for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    }
    for (std::size_t i = 0; i + 1 < v.size(); i++) {
        if (!v[i].instr || v[i].mnem != "B" || v[i].pred.empty() || !labels.count(v[i].ops[0]) || !isLabel(v[i + 1])) continue;
        std::size_t h = i; int uses = 0;
        for (std::size_t k = 0; k < i; k++) if (isLabel(v[k]) && labelName(v[k]) == v[i].ops[0]) h = k;
        for (std::size_t k = 0; k < v.size(); k++) if (v[k].instr) for (std::size_t o = 0; o < v[k].ops.size(); o++) if (v[k].ops[o] == v[i].ops[0]) uses++;
        if (h == i || uses != 1 || h == 0) continue;
        std::size_t g = h;
        while (g > 0 && v[g - 1].instr && !isBranch(v[g - 1].mnem)) g--;
        if (g == 0 || !v[g - 1].instr || v[g - 1].mnem != "B" || v[g - 1].pred.empty()) continue;
        std::vector<std::size_t> body;
        bool ok = true;
        for (std::size_t k = h + 1; k < i && ok; k++) {
            if (isLabel(v[k])) { ok = passThrough(v[k], named); continue; }
            if (!v[k].instr || !v[k].pred.empty()) ok = false; else body.push_back(k);
        }
        if (!ok || body.size() != 3) continue;
        // The three: the step, the store through the stepped pointer, and the compare the branch reads.
        std::string iv, V, P, n, C = v[i].pred[0] == '!' ? v[i].pred.substr(1) : v[i].pred;
        const bool negated = v[i].pred[0] == '!';
        long k = 0; std::size_t stepAt = 0, cmpAt = 0, storeAt = 0; bool plusOne = false;
        for (std::size_t b = 0; b < 3; b++) {
            const Line &l = v[body[b]];
            if (l.mnem == "ADD" && l.ops.size() == 3 && ((l.ops[0] == "1" && l.ops[1] == l.ops[2]) || (l.ops[1] == "1" && l.ops[0] == l.ops[2])) && iv.empty()) { iv = l.ops[2]; stepAt = b; }
            else if (isStore(l.mnem) && l.ops.size() == 2 && P.empty() && (l.mnem == "STB" || l.mnem == "STH" || l.mnem == "STW")) {
                long by = 0;
                P = steppedRegister(l, by); V = l.ops[0]; k = accessSize(l.mnem); storeAt = b;
                if (by != k || P.empty() || !sideOf(V)) P.clear();
            } else if ((l.mnem == "CMPLT" || l.mnem == "CMPGT") && l.ops.size() == 3 && l.ops[2] == C && cmpAt == 0 && b > 0) cmpAt = b;
        }
        if (iv.empty() || P.empty() || cmpAt == 0 || cmpAt < stepAt) continue;
        const Line &c = v[body[cmpAt]];
        const bool lt = c.mnem == "CMPLT";
        if (lt == !negated) { if (c.ops[0] != iv) continue; n = c.ops[1]; }
        else { if (c.ops[1] != iv) continue; n = c.ops[0]; }
        plusOne = negated;
        if (n == iv || n == P || n == V || P == iv || V == iv || V == P || P == "A15" || P == "B15" || (!isNumber(n) && !sideOf(n))) continue;
        if (storeAt == cmpAt || (liveOutAt(v, i) & bitOf(C)) || (liveInAt(v, i + 1) & bitOf(C))) continue;
        const std::uint64_t liveAtExits = liveOutAt(v, i) | liveInAt(v, i + 1), liveHead = liveInAt(v, h);
        std::set<std::string> used;
        used.insert(iv); used.insert(P); used.insert(V); used.insert(C); if (!isNumber(n)) used.insert(n);
        const char S = sideOf(P), SV = sideOf(V);
        // Seven registers beside the pointer and a condition, from what the loop never names and nothing after it reads.
        auto takeReg = [&](char side, bool pair) -> std::string {
            for (int kk = 3; kk < 32; kk++) {
                if ((kk >= 10 && kk <= 15) || (side == 'B' && kk == 3) || (pair && kk % 2)) continue;
                std::string r = std::string(1, side) + std::to_string(kk), r2 = std::string(1, side) + std::to_string(kk + 1);
                if (used.count(r) || (liveAtExits & bitOf(r)) || (liveHead & bitOf(r))) continue;
                if (pair && (kk + 1 >= 32 || used.count(r2) || (liveAtExits & bitOf(r2)) || (liveHead & bitOf(r2)))) continue;
                used.insert(r); if (pair) used.insert(r2);
                return r;
            }
            return "";
        };
        std::string cond;
        for (int kk = 0; kk < 3 && cond.empty(); kk++) {
            std::string r = std::string(1, S) + std::to_string(kk);
            if (!used.count(r) && !(liveAtExits & bitOf(r)) && !(liveHead & bitOf(r))) { cond = r; used.insert(r); }
        }
        const std::string K = takeReg(S, false), A = takeReg(S, false), M = takeReg(S, false), T = takeReg(S, false), X = takeReg(S, false), lo = takeReg(SV, true);
        if (cond.empty() || K.empty() || A.empty() || M.empty() || T.empty() || X.empty() || lo.empty()) continue;
        const std::string hi = std::string(1, lo[0]) + std::to_string(std::atoi(lo.c_str() + 1) + 1);
        const int log2k = k == 1 ? 0 : k == 2 ? 1 : 2, per = 3 - log2k;
        std::vector<Line> out;
        bool legal = true;
        // A line with a form, a crossed first source brought over through X where the machine has none for it.
        auto put = [&](Line l) {
            if (legalForm(l)) { out.push_back(l); return; }
            if (l.ops.size() == 3 && sideOf(l.ops[0]) && sideOf(l.ops[0]) != sideOf(l.ops[2])) {
                Line mv = make("MV", l.ops[0], X), again = rebuilt(l.mnem, std::vector<std::string>{ X, l.ops[1], l.ops[2] }, l.pred);
                if (legalForm(mv) && legalForm(again)) { out.push_back(mv); out.push_back(again); return; }
            }
            legal = false;
        };
        // Each loop is named `$fill`: memory-bound on this part, so the pipeliner leaves them as written rather than unroll them.
        const std::string base = labelName(v[h]), Lh = base + "$fh$fill", Lw = base + "$fw", Lk = base + "$fw$fill", Lt = base + "$ft$fill", exit = labelName(v[i + 1]);
        auto branch = [&](const std::string &to, const std::string &pred) { out.push_back(rebuilt("B", std::vector<std::string>(1, to), pred)); };
        if (isNumber(n)) {
            const long nk = std::atol(n.c_str());
            if (nk >= -32768 && nk <= 32767) put(make("MVK", n, K)); else { put(make("MVKL", n, K)); put(make("MVKH", n, K)); }
            put(make("SUB", K, iv, K));
        } else put(make("SUB", n, iv, K));
        if (plusOne) put(make("ADD", "1", K, K));
        put(make("CMPGT", "15", K, cond)); branch(base, cond);
        if (k == 1) { put(rebuilt("EXTU", std::vector<std::string>{ V, "24", "24", lo })); put(make("SHL", lo, "8", hi)); put(make("OR", lo, hi, lo)); put(make("SHL", lo, "16", hi)); put(make("OR", lo, hi, lo)); put(make("MV", lo, hi)); }
        else if (k == 2) { put(rebuilt("EXTU", std::vector<std::string>{ V, "16", "16", lo })); put(make("SHL", lo, "16", hi)); put(make("OR", lo, hi, lo)); put(make("MV", lo, hi)); }
        else { put(make("MV", V, lo)); put(make("MV", V, hi)); }
        put(make("NEG", P, A)); put(make("AND", "7", A, A)); if (log2k) put(make("SHR", A, std::to_string(log2k), A));
        put(make("SUB", K, A, M)); put(make("SHR", M, std::to_string(per), M));
        put(make("SHL", M, std::to_string(per), T)); put(make("SUB", K, T, T)); put(make("SUB", T, A, T));
        put(make("ADD", K, iv, iv));
        put(make("CMPEQ", "0", A, cond)); branch(Lw, cond);
        out.push_back(parse(Lh + ":"));
        out.push_back(v[body[storeAt]]); put(make("SUB", A, "1", A)); put(make("CMPGT", A, "0", cond)); branch(Lh, cond);
        out.push_back(parse(Lh + "$x:"));
        out.push_back(parse(Lw + ":"));
        put(make("MVK", "0", X));
        out.push_back(parse(Lk + ":"));
        put(make("STDW", hi + ":" + lo, "*" + P + "++(8)")); put(make("ADD", "1", X, X)); put(make("CMPLT", X, M, cond)); branch(Lk, cond);
        out.push_back(parse(Lk + "$x:"));
        put(make("CMPEQ", "0", T, cond)); branch(exit, cond);
        out.push_back(parse(Lt + ":"));
        out.push_back(v[body[storeAt]]); put(make("SUB", T, "1", T)); put(make("CMPGT", T, "0", cond)); branch(Lt, cond);
        out.push_back(parse(Lt + "$x:"));
        branch(exit, "");
        if (!legal) continue;
        if (std::getenv("CPP11_FILL")) std::fprintf(stderr, "fill %s: %s of %s through %s, %ld-byte elements\n", base.c_str(), v[body[storeAt]].mnem.c_str(), V.c_str(), P.c_str(), k);
        i = applyLoop(v, h, out, i);
    }
}

// **An index scaled by the access width goes into the access**: `SHL I, log2(size), T` read by `ADD R, T, U`s whose U is first touched by a load or store through `*U` of that width (U dead after, or the load's own destination, R untouched between) becomes `*+R[I]`, the machine scaling a register offset itself, and the ADD goes; the SHL goes too once every reader folded.
// I stepped before the last access, or on the other side from an R, makes the SHL `MV I, T` and the accesses `*+R[T]` - which wants every reader folded and T on each R's side.
bool foldScaledIndex(std::vector<Line> &v, std::size_t i, const std::set<std::string> &named) {
    const Line &l = v[i];
    if (l.mnem != "SHL" || l.ops.size() != 3 || !isNumber(l.ops[1]) || !sideOf(l.ops[0]) || !sideOf(l.ops[2]) || l.ops[0] == l.ops[2]) return false;
    const std::string I = l.ops[0], T = l.ops[2];
    const long shift = std::atol(l.ops[1].c_str());
    if (shift < 0 || shift > 3) return false;
    struct Use { std::size_t add, access; std::string R, U; bool iWritten; };
    std::vector<Use> uses;
    std::vector<std::size_t> unfolded;                            // readers of T that are not such an ADD
    bool iWritten = false, tLive = true;
    std::size_t last = i;
    for (std::size_t j = i + 1; j < v.size(); j++) {
        if (isLabel(v[j]) && passThrough(v[j], named)) continue;
        if (blockEnd(v[j])) { tLive = !deadAfter(v, j - 1, T); break; }
        std::vector<std::string> r2, w2;
        readsAndWrites(v[j], r2, w2);
        if (has(w2, I)) iWritten = true;
        if (has(r2, T) && has(w2, T)) { unfolded.push_back(j); tLive = false; break; }   // reads T, then ends it
        if (has(w2, T)) { tLive = false; break; }                 // T written over: nothing after reads this value
        if (!has(r2, T)) continue;
        const Line &a = v[j];
        const bool shape = a.pred.empty() && a.mnem == "ADD" && a.ops.size() == 3 && a.ops[2] != T && a.ops[2] != I &&
                           ((a.ops[0] == T && sideOf(a.ops[1]) && a.ops[1] != T) || (a.ops[1] == T && sideOf(a.ops[0]) && a.ops[0] != T));
        if (!shape) { unfolded.push_back(j); continue; }
        Use u{ j, 0, a.ops[0] == T ? a.ops[1] : a.ops[0], a.ops[2], iWritten };
        if (u.R == "A15" || u.R == "B15" || u.U == "A15" || u.U == "B15" || u.R == I) { unfolded.push_back(j); continue; }
        for (std::size_t k = j + 1; k < v.size() && u.access == 0; k++) {
            if (isLabel(v[k]) && passThrough(v[k], named)) continue;
            if (blockEnd(v[k])) break;
            std::vector<std::string> r3, w3;
            readsAndWrites(v[k], r3, w3);
            const bool access = v[k].pred.empty() && v[k].ops.size() == 2 && (isLoad(v[k].mnem) || isStore(v[k].mnem));
            if (has(w3, u.R) || has(w3, T) || has(w3, I)) { if (!(access && isLoad(v[k].mnem) && v[k].ops[1] == u.R && !has(w3, T) && !has(w3, I))) break; }
            if (!has(r3, u.U) && !has(w3, u.U)) continue;
            if (!access) break;
            const std::size_t m = isStore(v[k].mnem) ? 1 : 0;
            if (v[k].ops[m] != "*" + u.U || accessSize(v[k].mnem) != (1L << shift)) break;
            std::vector<std::string> data;
            registersIn(v[k].ops[1 - m], data);
            if (has(data, u.R) || has(data, T) || has(data, I)) break;
            if (!(has(data, u.U) || deadAfter(v, k, u.U))) break;
            u.access = k;
        }
        if (u.access == 0) { unfolded.push_back(j); continue; }
        uses.push_back(u);
        if (u.access > last) last = u.access;
    }
    if (uses.empty()) return false;
    // Direct: I itself is the offset, so I must hold its value through the last folded access and sit on each R's side.
    bool direct = true;
    for (std::size_t k = 0; k < uses.size(); k++) direct = direct && !uses[k].iWritten && sideOf(I) == sideOf(uses[k].R);
    for (std::size_t j = i + 1; j <= last && direct; j++) {        // stepped between an ADD and its access, too
        std::vector<std::string> r2, w2;
        if (v[j].instr) { readsAndWrites(v[j], r2, w2); if (has(w2, I)) direct = false; }
    }
    if (!direct) {
        if (!unfolded.empty() || tLive) return false;
        for (std::size_t k = 0; k < uses.size(); k++) if (sideOf(T) != sideOf(uses[k].R)) return false;
    }
    const std::string X = direct ? I : T;
    std::vector<std::size_t> gone;
    for (std::size_t k = 0; k < uses.size(); k++) {
        const Line &u = v[uses[k].access];
        const std::size_t m = isStore(u.mnem) ? 1 : 0;
        std::vector<std::string> ops = u.ops;
        ops[m] = "*+" + uses[k].R + "[" + X + "]";
        v[uses[k].access] = rebuilt(u.mnem, ops);
        gone.push_back(uses[k].add);
    }
    if (direct && unfolded.empty() && !tLive) gone.push_back(i);
    else if (!direct) v[i] = rebuilt("MV", std::vector<std::string>{ I, T });
    std::sort(gone.begin(), gone.end());
    for (std::size_t k = gone.size(); k-- > 0;) v.erase(v.begin() + static_cast<long>(gone[k]));
    return true;
}

// **Addresses folded into the access**: `ADD R, k, T` whose one reader is a load or store through `*T`, T dead
// after, is `*+R(k)`; and `*R` followed by `ADD k, R, R` with nothing touching R between, k whole elements
// up to 31, is `*R++` or `*R++[n]` and the ADD goes - `SUB` the same way with a minus.
void foldAddressing(std::vector<Line> &v) {
    LivenessHeld held(v, "foldAddressing");   // An address goes into its access later in the run, the register it lived in dead after; I and R stay unwritten.
    std::set<std::string> named;
    for (std::size_t i = 0; i < v.size(); i++) for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    for (std::size_t i = 0; i + 1 < v.size(); i++) {
        const Line &l = v[i];
        if (!l.instr || !l.pred.empty()) continue;
        if (!skipped("index") && foldScaledIndex(v, i, named)) { i--; continue; }
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        // `ADD k, R, R` whose next touch of R is an access through `*R` is that access through `*++R(k)`.
        if ((l.mnem == "ADD" || l.mnem == "SUB") && l.ops.size() == 3 && writes.size() == 1 && l.ops[2] == writes[0] &&
            sideOf(writes[0]) && writes[0] != "A15" && writes[0] != "B15" && (isNumber(l.ops[0]) != isNumber(l.ops[1])) &&
            (isNumber(l.ops[0]) ? l.ops[1] : l.ops[0]) == writes[0] && !(l.mnem == "SUB" && isNumber(l.ops[0]))) {
            const std::string R = writes[0];
            long k = std::atol((isNumber(l.ops[0]) ? l.ops[0] : l.ops[1]).c_str());
            if (l.mnem == "SUB") k = -k;
            for (std::size_t j = i + 1; j < v.size(); j++) {
                if (isLabel(v[j]) && passThrough(v[j], named)) continue;
                if (blockEnd(v[j])) break;
                std::vector<std::string> r2, w2;
                readsAndWrites(v[j], r2, w2);
                if (!has(r2, R) && !has(w2, R)) continue;
                const Line &u = v[j];
                if (u.pred.empty() && u.ops.size() == 2 && (isLoad(u.mnem) || isStore(u.mnem))) {
                    const std::size_t at = isStore(u.mnem) ? 1 : 0;
                    const long size = accessSize(u.mnem), mag = k < 0 ? -k : k;
                    std::vector<std::string> data;
                    registersIn(u.ops[1 - at], data);
                    if (u.ops[at] == "*" + R && mag != 0 && mag % size == 0 && mag <= 31 && !has(data, R)) {
                        std::vector<std::string> ops = u.ops;
                        ops[at] = (k < 0 ? "*--" : "*++") + R + "(" + std::to_string(mag) + ")";
                        v[j] = rebuilt(u.mnem, ops);
                        v.erase(v.begin() + static_cast<long>(i));
                        i--;
                    }
                }
                break;
            }
            continue;
        }
        if ((l.mnem == "ADD" || l.mnem == "SUB" || l.mnem == "SUBAW") && l.ops.size() == 3 && writes.size() == 1 && !has(reads, writes[0])) {
            const bool k0 = isNumber(l.ops[0]), k1 = isNumber(l.ops[1]);
            if (l.mnem != "ADD" && !k1) continue;
            if (k0 == k1) continue;
            const std::string R = k0 ? l.ops[1] : l.ops[0], T = writes[0];
            long k = std::atol((k0 ? l.ops[0] : l.ops[1]).c_str());
            if (l.mnem == "SUBAW") k *= 4;             // scaled by the word
            if (l.mnem != "ADD") k = -k;
            if (!sideOf(R) || R == T) continue;
            for (std::size_t j = i + 1; j < v.size(); j++) {
                if (isLabel(v[j]) && passThrough(v[j], named)) continue;
                if (blockEnd(v[j])) break;
                std::vector<std::string> r2, w2;
                readsAndWrites(v[j], r2, w2);
                const bool access = v[j].pred.empty() && v[j].ops.size() == 2 && (isLoad(v[j].mnem) || isStore(v[j].mnem));
                if (has(w2, R) && !(access && isLoad(v[j].mnem) && v[j].ops[1] == R)) break;   // a load into R reads R first
                if (!has(r2, T) && !has(w2, T)) continue;
                const Line &u = v[j];
                if (access) {
                    const std::size_t at = isStore(u.mnem) ? 1 : 0;
                    const long size = accessSize(u.mnem), mag = k < 0 ? -k : k;
                    std::vector<std::string> data;
                    registersIn(u.ops[1 - at], data);
                    if (u.ops[at] == "*" + T && mag % size == 0 && mag / size <= 31 && (!has(data, R) || isLoad(u.mnem)) && (has(data, T) || deadAfter(v, j, T))) {
                        std::vector<std::string> ops = u.ops;
                        ops[at] = mag == 0 ? "*" + R : (k < 0 ? "*-" : "*+") + R + "(" + std::to_string(mag) + ")";
                        v[j] = rebuilt(u.mnem, ops);
                        v.erase(v.begin() + static_cast<long>(i));
                        i--;
                    }
                }
                break;
            }
            continue;
        }
        if ((isLoad(l.mnem) || isStore(l.mnem)) && l.ops.size() == 2) {
            const std::size_t at = isStore(l.mnem) ? 1 : 0;
            if (l.ops[at].size() < 2 || l.ops[at][0] != '*' || !sideOf(l.ops[at].substr(1))) continue;
            const std::string R = l.ops[at].substr(1);
            std::vector<std::string> data;
            registersIn(l.ops[1 - at], data);
            if (has(data, R) || R == "A15" || R == "B15") continue;      // a push keeps its shape
            for (std::size_t j = i + 1; j < v.size(); j++) {
                if (isLabel(v[j]) && passThrough(v[j], named)) continue;
                if (blockEnd(v[j])) break;
                std::vector<std::string> r2, w2;
                readsAndWrites(v[j], r2, w2);
                if (!has(r2, R) && !has(w2, R)) continue;
                const Line &u = v[j];
                const bool k0 = u.ops.size() == 3 && isNumber(u.ops[0]), k1 = u.ops.size() == 3 && isNumber(u.ops[1]);
                if (u.pred.empty() && (u.mnem == "ADD" || u.mnem == "SUB") && u.ops.size() == 3 && u.ops[2] == R && (k0 != k1) &&
                    (k0 ? u.ops[1] : u.ops[0]) == R && !(u.mnem == "SUB" && k0)) {
                    long k = std::atol((k0 ? u.ops[0] : u.ops[1]).c_str());
                    if (u.mnem == "SUB") k = -k;
                    const long size = accessSize(l.mnem), mag = k < 0 ? -k : k;
                    if (mag != 0 && mag % size == 0 && mag / size <= 31) {
                        std::vector<std::string> ops = l.ops;
                        ops[at] = "*" + R + (k < 0 ? "--" : "++") + "(" + std::to_string(mag) + ")";
                        v[i] = rebuilt(l.mnem, ops);
                        v.erase(v.begin() + static_cast<long>(j));
                    }
                }
                break;
            }
        }
    }
}

// **Jumps tidied**: what follows an unconditional branch up to the next label never runs; a branch to
// a label whose first instruction is an unconditional jump goes where that one goes; and a branch to
// the next label - or, last in the text, to the epilogue after it - is nothing.
void tidyJumps(std::vector<Line> &v) {
    std::set<std::string> labels;
    for (std::size_t i = 0; i < v.size(); i++) if (isLabel(v[i])) labels.insert(labelName(v[i]));
    std::vector<Line> out;
    for (std::size_t i = 0; i < v.size(); i++) {
        out.push_back(v[i]);
        if (!v[i].instr || v[i].mnem != "B" || !v[i].pred.empty() || isCall(v[i], labels)) continue;
        while (i + 1 < v.size() && v[i + 1].instr) i++;
    }
    v.swap(out);
    std::map<std::string, std::string> onward;          // a label whose first instruction is `B M`
    for (std::size_t i = 0; i < v.size(); i++) {
        if (!isLabel(v[i])) continue;
        std::size_t j = i + 1;
        while (j < v.size() && isLabel(v[j])) j++;
        if (j < v.size() && v[j].instr && v[j].mnem == "B" && v[j].pred.empty() && !isCall(v[j], labels) && !sideOf(v[j].ops[0]))
            onward[labelName(v[i])] = v[j].ops[0];
    }
    for (std::size_t i = 0; i < v.size(); i++) {
        if (!v[i].instr || v[i].mnem != "B" || sideOf(v[i].ops[0]) || isCall(v[i], labels)) continue;
        std::string t = v[i].ops[0];
        for (int hops = 0; hops < 8 && onward.count(t) && onward[t] != t; hops++) t = onward[t];
        if (t != v[i].ops[0]) v[i] = rebuilt("B", std::vector<std::string>(1, t), v[i].pred);
    }
    out.clear();
    for (std::size_t i = 0; i < v.size(); i++) {
        if (v[i].instr && v[i].mnem == "B" && !sideOf(v[i].ops[0]) && !isCall(v[i], labels)) {
            std::size_t j = i + 1;
            bool next = false;
            for (; j < v.size() && !v[j].instr; j++) if (isLabel(v[j]) && labelName(v[j]) == v[i].ops[0]) next = true;
            if (!next && j == v.size() && isReturn(v[i], labels)) next = true;
            if (next) continue;
        }
        out.push_back(v[i]);
    }
    v.swap(out);
}

bool isPush(const std::vector<Line> &v, std::size_t i) {
    return i + 1 < v.size() && v[i].raw == "\tSUB\tB15, 8, B15" && isStore(v[i + 1].mnem) &&
           v[i + 1].ops.size() == 2 && ((v[i + 1].ops[1] == "*B15" && (v[i + 1].mnem == "STW" || v[i + 1].mnem == "STDW")) ||
                                     (v[i + 1].ops[1] == "*+B15(4)" && v[i + 1].mnem == "STW"));
}
bool isPop(const std::vector<Line> &v, std::size_t i) {
    return i + 1 < v.size() && (v[i].mnem == "LDW" || v[i].mnem == "LDDW") && v[i].ops.size() == 2 &&
           (v[i].ops[0] == "*B15" || (v[i].ops[0] == "*+B15(4)" && v[i].mnem == "LDW")) && v[i + 1].raw == "\tADD\tB15, 8, B15";
}

// A push whose pop is in the same block, with no call and no other use of
// B15 between, keeps its value in A16-A31 instead: the pair's slot d takes
// A(16+2d):A(17+2d), and the two stack adjustments go with the memory access.
void foldPushPop(std::vector<Line> &v) {
    LivenessHeld held(v, "foldPushPop");   // A pair's registers are written and read between its push and its pop, in one block.
    std::vector<std::size_t> open;             // pushes not yet popped, this block
    std::vector<std::pair<std::size_t, std::size_t> > pairs;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (blockEnd(v[i])) { open.clear(); continue; }
        if (isPush(v, i)) { open.push_back(i); i++; continue; }
        if (isPop(v, i)) {
            if (!open.empty()) { pairs.push_back(std::make_pair(open.back(), i)); open.pop_back(); }
            i++;
            continue;
        }
        std::vector<std::string> reads, writes;
        readsAndWrites(v[i], reads, writes);
        if (has(reads, "B15") || has(writes, "B15")) open.clear();
    }
    std::vector<bool> drop(v.size(), false);
    for (std::size_t p = 0; p < pairs.size(); p++) {
        std::size_t push = pairs[p].first, pop = pairs[p].second;
        int depth = 0;
        for (std::size_t q = 0; q < pairs.size(); q++)
            if (pairs[q].first < push && pairs[q].second > pop) depth++;
        // The pair's registers must be dead at the push and untouched up to the pop - a leaf's local may
        // live in A16-A23 - so the depth steps up past any that are not.
        std::string lo, hi;
        for (; depth < 8 && lo.empty(); depth++) {
            const std::string l = "A" + std::to_string(16 + 2 * depth), h = "A" + std::to_string(17 + 2 * depth);
            bool ok = deadAfter(v, push, l) && deadAfter(v, push, h);
            for (std::size_t k = push + 1; k <= pop + 1 && ok; k++) {
                std::vector<std::string> r, w;
                readsAndWrites(v[k], r, w);
                if (has(r, l) || has(w, l) || has(r, h) || has(w, h)) ok = false;
            }
            if (ok) { lo = l; hi = h; }
        }
        if (lo.empty()) continue;
        std::string src = v[push + 1].ops[0], dst = v[pop].ops[1];
        if (v[push + 1].mnem == "STDW") {      // the pair's halves take the two stack lines
            v[push] = make("MV", src.substr(src.find(':') + 1), lo);
            v[push + 1] = make("MV", src.substr(0, src.find(':')), hi);
            v[pop] = make("MV", lo, dst.substr(dst.find(':') + 1));
            v[pop + 1] = make("MV", hi, dst.substr(0, dst.find(':')));
            continue;
        }
        v[push + 1] = make("MV", src, lo);
        v[pop] = make("MV", lo, dst);
        drop[push] = true;
        drop[pop + 1] = true;
    }
    std::vector<Line> out;
    for (std::size_t i = 0; i < v.size(); i++) if (!drop[i]) out.push_back(v[i]);
    v.swap(out);
}

// ----- the list scheduler -----


// The units a mnemonic has a form on for these operands, from asm6x's table
// (ASM6x src/forms.h): the three-operand arithmetic on any of .L .S .D with a
// register, the small constants on .L and .S, the wider ones on .D alone.
unsigned unitsFor(const Line &l) {
    const std::string &m = l.mnem;
    if (isBranch(m) || m == "ADDK") return US;
    if (isLoad(m) || isStore(m)) return UD;
    if (m == "MPY32" || m == "MPY32U" || m == "MPYSP" || m == "MPYDP" || startsWith(m, "MPY")) return UM;
    bool pair = false;
    for (std::size_t o = 0; o < l.ops.size(); o++) if (l.ops[o].find(':') != std::string::npos) pair = true;
    long k = 0; int at = -1;
    if (l.ops.size() == 3) { if (isNumber(l.ops[0])) { at = 0; k = std::atol(l.ops[0].c_str()); } else if (isNumber(l.ops[1])) { at = 1; k = std::atol(l.ops[1].c_str()); } }
    bool small = at >= 0 && k >= -16 && k <= 15, ucst = at >= 0 && k >= 0 && k <= 31;
    if (m == "ADD" || m == "AND" || m == "OR" || m == "XOR") {
        if (pair) return UL;
        if (at < 0) return UL | US | UD;
        if (small) return UL | US | (ucst ? UD : 0);
        return ucst && m == "ADD" && crossings(l) == 0 ? UD : 0;
    }
    if (m == "SUBAW" || m == "ADDAW") return crossings(l) == 0 ? UD : 0;   // .D alone, the sources on its side
    if (m == "SUB") {
        if (pair) return UL;
        if (at < 0) {               // a crossed first source has the .S form and .L's crossed-first one, never .D's
            std::vector<std::string> first, dst;
            registersIn(l.ops[0], first); registersIn(l.ops[2], dst);
            bool firstCrosses = !first.empty() && !dst.empty() && sideOf(first[0]) != sideOf(dst[0]);
            return firstCrosses ? UL | US : UL | US | UD;
        }
        if (at == 0 && small) return UL | US;
        if (at == 1 && ucst && crossings(l) == 0) return UD;
        return 0;
    }
    if (m == "NOT") return UL | US | UD;
    if (m == "NEG") return UL | US;
    if (m == "MV") return pair ? UL : UL | US | UD;
    if (m == "ZERO") return UL;
    if (m == "CLR" || m == "EXT" || m == "EXTU" || m == "SHL" || m == "SHR" || m == "SHRU") return US;
    if (m == "MVK") return isNumber(l.ops[0]) && std::atol(l.ops[0].c_str()) >= -16 && std::atol(l.ops[0].c_str()) <= 15 ? UL | US : US;
    if (m == "MVKL" || m == "MVKH") return US;
    if (m == "CMPEQ" || m == "CMPGT" || m == "CMPLT" || m == "CMPGTU" || m == "CMPLTU") return UL;
    if (endsWith(m, "SP") || endsWith(m, "DP")) {
        if (startsWith(m, "CMP") || m == "SPDP") return US;
        if (m == "ADDDP" || m == "SUBDP") return crossings(l) > 0 ? US : UL | US;   // the xdp form is .S's alone
        if (m == "ADDSP" || m == "SUBSP") return UL | US;
        if (m == "DPSP" || m == "SPTRUNC" || m == "DPTRUNC" || startsWith(m, "INT")) return UL;
    }
    return 0;
}

// The frame slot a memory operand names, `*-A15(k)` or `*+A15(k)`, so two such accesses that do not overlap need no order.
bool frameSlot(const std::string &op, long &off) {
    if (op == "*A15") { off = 0; return true; }
    if (!startsWith(op, "*-A15(") && !startsWith(op, "*+A15(")) return false;
    std::string k = op.substr(6, op.size() - 7);
    if (!isNumber(k)) return false;
    off = std::atol(k.c_str()) * (op[1] == '-' ? -1 : 1);
    return true;
}

Node makeNode(const Line &l, const std::set<std::string> &labels) {
    Node n;
    n.line = l;
    std::vector<std::string> reads, writes;
    readsAndWrites(l, reads, writes);
    n.reads = maskOf(reads);
    n.writes = maskOf(writes);
    n.lat = delaySlots(l.mnem) + 1;
    n.late = lateReads(l.mnem);
    n.units = unitsFor(l);
    if (isBranch(l.mnem)) {
        n.branch = true;
        n.call = isCall(l, labels);
        if (n.call) { n.reads |= callReads(); n.writes |= callWrites(); }
        if (l.mnem == "CALLP") { n.reads &= ~bitOf("B3"); n.side = 1; return n; }
        if (sideOf(l.ops[0])) { n.side = 1; if (sideOf(l.ops[0]) == 'A') n.cross = 1; }
        return n;
    }
    if (isLoad(l.mnem) || isStore(l.mnem)) {
        n.mem = true;
        n.store = isStore(l.mnem);
        const std::string &addr = n.store ? l.ops[1] : l.ops[0];
        const std::string &data = n.store ? l.ops[0] : l.ops[1];
        std::vector<std::string> a, d;
        registersIn(addr, a);
        registersIn(data, d);
        n.side = a.empty() ? 0 : sideIndex(a[0]);
        n.tpath = d.empty() ? 0 : sideIndex(d[0]);
        n.memSize = accessSize(l.mnem);
        n.memFrame = frameSlot(addr, n.memOff);
        long by;
        n.stepped = bitOf(steppedRegister(l, by));
        return n;
    }
    std::vector<std::string> dst;
    if (!l.ops.empty()) registersIn(l.ops.back(), dst);
    n.side = dst.empty() ? 0 : sideIndex(dst[0]);
    if (crossings(l) > 0) n.cross = n.side;
    return n;
}

// Two memory accesses are independent when both are frame slots that do not overlap.
bool mayAlias(const Node &a, const Node &b) {
    if (!a.memFrame || !b.memFrame) return true;
    return a.memOff < b.memOff + b.memSize && b.memOff < a.memOff + a.memSize;
}

// Whether every member, with the candidate added, can be given a unit: a
// depth-first assignment over the four letters of a side, the side of a
// branch to a label chosen too.
bool assignUnits(const std::vector<const Node *> &m, std::size_t k, bool taken[2][4]) {
    if (k == m.size()) return true;
    const Node *n = m[k];
    for (int side = 0; side < 2; side++) {
        if (n->side >= 0 && n->side != side) continue;
        for (int u = 0; u < 4; u++) {
            if (!(n->units & (1u << u)) || taken[side][u]) continue;
            taken[side][u] = true;
            if (assignUnits(m, k + 1, taken)) { taken[side][u] = false; return true; }
            taken[side][u] = false;
        }
    }
    return false;
}

bool fits(const Packet &p, const Node &n) {
    if (p.alone || p.count >= 8) return false;
    if (n.units == 0) return p.count == 0;
    if (n.branch && p.branch) return false;
    if (n.cross >= 0 && p.cross[n.cross]) return false;
    if (n.mem && p.tpath[n.tpath]) return false;
    if (n.writes & p.writes) return false;
    std::vector<const Node *> m = p.members;
    m.push_back(&n);
    bool taken[2][4];
    for (int s = 0; s < 2; s++) for (int u = 0; u < 4; u++) taken[s][u] = p.held[s][u];
    return assignUnits(m, 0, taken);
}

void add(Packet &p, const Node &n) {
    p.members.push_back(&n);
    p.count++;
    if (n.units == 0) p.alone = true;
    if (n.branch) p.branch = true;
    if (n.cross >= 0) p.cross[n.cross] = true;
    if (n.mem) p.tpath[n.tpath] = true;
    p.writes |= n.writes;
}

struct Pending { std::uint64_t writes; int lands; };   // a write in flight where a block ends, for the one fallen into

// One block - the instructions between two labels, ending in its branch if it has one - scheduled by list scheduling: the edges between instructions
// are the register and memory orders with their distances in cycles, each cycle takes the ready instructions of greatest height that its packet has
// units for, and the branch goes as early as leaves every other instruction issued in its window and landed by the time its target runs.
std::vector<Pending> scheduleBlock(std::vector<Node> &nodes, std::ostringstream &out, const std::vector<Pending> &in, bool carry) {
    const std::size_t n = nodes.size();
    int branchAt = -1, landing = 0;
    std::vector<int> minIssue(n, 0);                     // what the block before still has in flight
    for (std::size_t p = 0; p < in.size(); p++) {
        landing = std::max(landing, in[p].lands);
        for (std::size_t i = 0; i < n; i++) {
            if (nodes[i].reads & in[p].writes) minIssue[i] = std::max(minIssue[i], in[p].lands);
            if (nodes[i].writes & in[p].writes) minIssue[i] = std::max(minIssue[i], in[p].lands - nodes[i].lat + 1);
        }
    }
    for (std::size_t i = 0; i < n; i++) if (nodes[i].branch) branchAt = static_cast<int>(i);
    for (std::size_t i = 0; i < n; i++) {
        Node &b = nodes[i];
        for (std::size_t j = 0; j < i; j++) {
            const Node &a = nodes[j];
            int d = -1;
            if (a.writes & b.reads) d = std::max(d, (a.writes & ~a.stepped & b.reads) ? a.lat : 1);
            if (a.reads & b.writes) d = std::max(d, std::max(a.store ? 1 : 0, a.late - b.lat + 1));
            if (a.writes & b.writes) d = std::max(d, std::max(1, a.lat - b.lat + 1));
            if (a.mem && b.mem && (a.store || b.store) && mayAlias(a, b)) d = std::max(d, 1);
            if (d >= 0) b.preds.push_back(std::make_pair(static_cast<int>(j), d));
        }
    }
    for (std::size_t i = n; i-- > 0;) {
        Node &a = nodes[i];
        a.height = a.lat;
        for (std::size_t k = i + 1; k < n; k++)
            for (std::size_t e = 0; e < nodes[k].preds.size(); e++)
                if (nodes[k].preds[e].first == static_cast<int>(i)) a.height = std::max(a.height, nodes[k].preds[e].second + nodes[k].height);
    }
    std::vector<Packet> cycles;
    std::size_t left = n - (branchAt >= 0 ? 1 : 0);
    for (int c = 0; left > 0; c++) {
        if (static_cast<int>(cycles.size()) <= c) cycles.push_back(Packet());
        for (;;) {
            Packet &p = cycles[static_cast<std::size_t>(c)];   // taken afresh: holding a unit may grow the vector
            int best = -1;
            for (std::size_t i = 0; i < n; i++) {
                Node &x = nodes[i];
                if (x.issue >= 0 || x.branch || c < minIssue[i]) continue;
                bool ready = true;
                for (std::size_t e = 0; e < x.preds.size() && ready; e++) {
                    const Node &y = nodes[static_cast<std::size_t>(x.preds[e].first)];
                    if (y.branch) continue;
                    if (y.issue < 0 || y.issue + x.preds[e].second > c) ready = false;
                }
                if (!ready || !fits(p, x)) continue;
                if (best < 0 || x.height > nodes[static_cast<std::size_t>(best)].height) best = static_cast<int>(i);
            }
            if (best < 0) break;
            Node &chosen = nodes[static_cast<std::size_t>(best)];
            chosen.issue = c;
            add(p, chosen);
            left--;
            // A DP instruction holds its unit on its side - each it might be given - for its late cycles, and a crossed pair the cross path.
            for (int k = 1; k <= chosen.late; k++) {
                while (static_cast<int>(cycles.size()) <= c + k) cycles.push_back(Packet());
                for (int side = 0; side < 2; side++) {
                    if (chosen.side >= 0 && chosen.side != side) continue;
                    for (int u = 0; u < 4; u++) if (chosen.units & (1u << u)) cycles[static_cast<std::size_t>(c + k)].held[side][u] = true;
                }
                if (chosen.cross >= 0) cycles[static_cast<std::size_t>(c + k)].cross[chosen.cross] = true;
            }
        }
    }
    int length = 0;
    for (std::size_t i = 0; i < n; i++) if (!nodes[i].branch) length = std::max(length, nodes[i].issue + nodes[i].lat);
    if (branchAt >= 0) {
        Node &b = nodes[static_cast<std::size_t>(branchAt)];
        int at = std::max(0, landing - 6);
        for (std::size_t e = 0; e < b.preds.size(); e++) {
            const Node &y = nodes[static_cast<std::size_t>(b.preds[e].first)];
            at = std::max(at, y.issue + b.preds[e].second);
        }
        for (std::size_t i = 0; i < n; i++) {
            if (nodes[i].branch) continue;
            at = std::max(at, nodes[i].issue - (b.line.mnem == "CALLP" ? 0 : 5));   // issued in the window, and CALLP's is NOPs
            at = std::max(at, nodes[i].issue + nodes[i].lat - 6);  // landed when the target runs
        }
        for (;;) {
            while (static_cast<int>(cycles.size()) <= at) cycles.push_back(Packet());
            if (fits(cycles[static_cast<std::size_t>(at)], b)) break;
            at++;
        }
        b.issue = at;
        add(cycles[static_cast<std::size_t>(at)], b);
        length = b.line.mnem == "CALLP" ? at + 1 : at + 6;
    }
    // Falling into a label: the block ends at its last issue, its late landings handed on; else it waits for them.
    std::vector<Pending> pend;
    if (carry && branchAt < 0) {
        length = 0;
        for (std::size_t i = 0; i < n; i++) length = std::max(length, nodes[i].issue + 1 + nodes[i].late);
        for (std::size_t i = 0; i < n; i++)
            if (nodes[i].issue + nodes[i].lat > length) { Pending p = { nodes[i].writes, nodes[i].issue + nodes[i].lat - length }; pend.push_back(p); }
        for (std::size_t p = 0; p < in.size(); p++)
            if (in[p].lands > length) { Pending q = { in[p].writes, in[p].lands - length }; pend.push_back(q); }
    } else length = std::max(length, landing);
    int nops = 0;
    for (int c = 0; c < length; c++) {
        const Packet *p = c < static_cast<int>(cycles.size()) ? &cycles[static_cast<std::size_t>(c)] : nullptr;
        if (!p || p->count == 0) { nops++; continue; }
        while (nops > 0) { out << "\tNOP\t" << std::min(nops, 9) << "\n"; nops -= std::min(nops, 9); }
        for (std::size_t k = 0; k < p->members.size(); k++) out << (k == 0 ? "" : "||") << p->members[k]->line.raw << "\n";
    }
    while (nops > 0) { out << "\tNOP\t" << std::min(nops, 9) << "\n"; nops -= std::min(nops, 9); }
    return pend;
}

// A label of the backend's own flow - the kinds only a branch of this text
// names - that no instruction names is no block boundary: the instructions
// on either side are scheduled together and the label is kept in front.
bool passThrough(const Line &l, const std::set<std::string> &named) {
    static const char *const kinds[] = { ".begin", ".step", ".end", ".else", ".shortcut", ".wide", ".widend", ".noresult", ".case", ".default", ".inline" };
    const std::string name = labelName(l);   // spelled with dots here; the TI spelling makes them $ later
    if (named.count(name)) return false;
    std::size_t d = name.find_last_not_of("0123456789");
    if (d == std::string::npos || d + 1 >= name.size()) return false;
    std::size_t k = name.rfind('.', d);
    if (k == std::string::npos) return false;
    const std::string kind = name.substr(k, d + 1 - k);
    for (std::size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) if (kind == kinds[i]) return true;
    return false;
}

// The whole text: each block scheduled on its own, every label and directive kept where it is.
std::string schedule(const std::vector<Line> &v) {
    std::set<std::string> labels, named;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) labels.insert(labelName(v[i]));
        for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    }
    std::ostringstream out;
    std::vector<Node> block;
    std::vector<Pending> pend;
    // A label is fallen into with the block's late writes in flight; anything else waits for them to land.
    auto flush = [&](bool carry) {
        if (!block.empty()) pend = scheduleBlock(block, out, pend, carry);
        else if (!carry) {
            int wait = 0;
            for (std::size_t p = 0; p < pend.size(); p++) wait = std::max(wait, pend[p].lands);
            while (wait > 0) { out << "\tNOP\t" << std::min(wait, 9) << "\n"; wait -= std::min(wait, 9); }
            pend.clear();
        }
        block.clear();
    };
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i]) && passThrough(v[i], named)) { out << v[i].raw << "\n"; continue; }
        if (v[i].verbatim) { flush(false); out << v[i].raw << "\n"; continue; }
        if (!v[i].instr) { flush(isLabel(v[i])); out << v[i].raw << "\n"; continue; }
        block.push_back(makeNode(v[i], labels));
        if (isBranch(v[i].mnem)) flush(true);
    }
    flush(false);
    return out.str();
}

// **A call is one CALLP**: `MVKL r, B3; MVKH r, B3; B f` with r the label after the branch is
// `CALLP f, B3`, which sets B3 itself and carries the five NOPs - one word for four, two cycles
// fewer. A call through a register keeps the branch.
void foldCalls(std::vector<Line> &v) {
    std::vector<Line> out;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (i + 3 < v.size() && v[i].mnem == "MVKL" && v[i + 1].mnem == "MVKH" && v[i + 2].mnem == "B" &&
            v[i].pred.empty() && v[i + 1].pred.empty() && v[i + 2].pred.empty() &&
            v[i].ops.size() == 2 && v[i + 1].ops.size() == 2 && v[i].ops[1] == "B3" && v[i + 1].ops[1] == "B3" &&
            v[i].ops[0] == v[i + 1].ops[0] && isLabel(v[i + 3]) && labelName(v[i + 3]) == v[i].ops[0] &&
            !v[i + 2].ops.empty() && !sideOf(v[i + 2].ops[0])) {
            out.push_back(make("CALLP", v[i + 2].ops[0], "B3"));
            i += 2;
            continue;
        }
        out.push_back(v[i]);
    }
    v.swap(out);
}

// **A branch and the NOPs after it are one BNOP**: a packet holding `B x` followed by `NOP n`
// is that packet with `BNOP x, min(n, 5)`. A label is folded only where the text is short
// enough for BNOP's reach (PCR_S12, +-8 KB), a register only on the B side.
std::string foldBranchNops(const std::string &text, bool near) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    for (std::size_t i = 0; i < lines.size(); i++) {
        bool member = startsWith(lines[i], "||");
        Line l = parse(member ? lines[i].substr(2) : lines[i]);
        if (!l.instr || l.mnem != "B" || l.ops.size() != 1) continue;
        if (sideOf(l.ops[0]) && sideOf(l.ops[0]) != 'B') continue;
        std::size_t j = i + 1;
        while (j < lines.size() && startsWith(lines[j], "||")) j++;
        // The side is named: an unnamed BNOP asks for .S1 first where B asks for .S2, and an
        // assembler moves one instruction at most to make room. .S2 where the packet allows it.
        std::string unit = ".S2";
        if (!sideOf(l.ops[0])) {
            std::size_t first = i;
            while (first > 0 && startsWith(lines[first], "||")) first--;
            std::vector<Node> nodes;
            std::set<std::string> none;
            for (std::size_t m = first; m < j; m++) nodes.push_back(makeNode(parse(startsWith(lines[m], "||") ? lines[m].substr(2) : lines[m]), none));
            std::vector<const Node *> members;
            for (std::size_t m = 0; m < nodes.size(); m++) { if (m + first == i) nodes[m].side = 1; members.push_back(&nodes[m]); }
            bool taken[2][4] = { { false, false, false, false }, { false, false, false, false } };
            if (!assignUnits(members, 0, taken)) unit = ".S1";
        }
        const std::string pred = l.pred.empty() ? std::string() : "[" + l.pred + "]\t";
        Line nop = j < lines.size() ? parse(lines[j]) : Line();
        const int n = nop.instr && nop.mnem == "NOP" && nop.ops.size() == 1 && nop.pred.empty() ? std::atoi(nop.ops[0].c_str()) : 0, k = std::min(n, 5);
        if (k <= 0 || (!sideOf(l.ops[0]) && !near)) {
            // No NOP to fold, or past BNOP's reach: a label branch sharing its packet still takes the side the packet leaves it.
            if (!sideOf(l.ops[0]) && (member || (i + 1 < lines.size() && startsWith(lines[i + 1], "||")))) lines[i] = (member ? "||" : "") + ("\t" + pred + "B\t" + unit + "\t" + l.ops[0]);
            continue;
        }
        std::string raw = "\t" + pred + "BNOP\t" + unit + "\t" + l.ops[0] + ", " + std::to_string(k);
        lines[i] = (member ? "||" : "") + raw;
        if (n > k) lines[j] = "\tNOP\t" + std::to_string(n - k);
        else lines.erase(lines.begin() + static_cast<long>(j));
    }
    std::string out;
    for (std::size_t i = 0; i < lines.size(); i++) out += lines[i] + "\n";
    return out;
}

}   // namespace c6x

using namespace c6x;

// CPP11_C6XSKIP names passes (comma-separated) to leave out, for bisecting a wrong answer to its pass.
bool skipped(const char *pass) {
    const char *e = std::getenv("CPP11_C6XSKIP");
    return e != nullptr && (std::string(",") + e + ",").find(std::string(",") + pass + ",") != std::string::npos;
}

// A result nothing reads - a copy, an address, a constant made and then overwritten - is not made; a load
// stays, every read of an object being a read here, and so does anything that writes memory or a pair.
bool removeDead(std::vector<Line> &v) {
    LivenessHeld held(v, "removeDead");   // What it removes is dead.
    bool changed = false;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &l = v[i];
        if (!l.instr || !l.pred.empty() || isBranch(l.mnem) || isStore(l.mnem) || isLoad(l.mnem) || l.ops.empty()) continue;
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        if (writes.size() != 1 || l.ops.back().find(':') != std::string::npos || l.ops.back()[0] == '*') continue;
        const std::string &d = writes[0];
        if (d == "A15" || d == "B15" || d == "B14" || d == "B3" || !deadAfter(v, i, d)) continue;
        v.erase(v.begin() + static_cast<long>(i));
        i--;
        changed = true;
    }
    return changed;
}

// The copy, constant and predicate folds, to a fixed point.
void rounds(std::vector<Line> &lines) {
    for (int round = 0; round < 8; round++) {
        computeLiveness(lines);
        bool changed = forwardMoves(lines);
        computeLiveness(lines);
        changed = (!skipped("pairmv") && forwardPairMoves(lines)) || changed;
        computeLiveness(lines);
        changed = forwardConstants(lines) || changed;
        computeLiveness(lines);
        changed = flipPredicates(lines) || changed;
        computeLiveness(lines);
        changed = (!skipped("dead") && removeDead(lines)) || changed;
        if (!changed) break;
    }
}

std::string c6xSchedule(const std::string &text, int level) {
    if (level <= 0) return text;
    std::vector<Line> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        Line l = parse(line);
        if (l.mnem != "NOP") lines.push_back(l);    // the -O0 padding, re-derived by the scheduler
    }
    foldCalls(lines);
    foldMvk(lines);
    computeLiveness(lines);
    foldFrame(lines);
    foldPushPop(lines);
    if (!skipped("thread")) threadPredicates(lines);
    if (!skipped("labels")) dropUnnamedLabels(lines);
    rounds(lines);
    if (!skipped("arms") && predicateArms(lines)) rounds(lines);
    rotateLoops(lines);
    tidyJumps(lines);
    if (!skipped("labels")) dropUnnamedLabels(lines);
    if (!skipped("entry")) reuseEntryLoads(lines);
    if (level >= 2) {
        hoistInvariants(lines);
        if (!skipped("induct")) inductionPointers(lines);
        if (!skipped("fill")) widenFills(lines);
        pipelineLoops(lines);
        if (!skipped("pairs")) hoistConstantPairs(lines);
    }
    // After the loops are made: a value numbered across a pipelined kernel would lengthen the spans it schedules.
    computeLiveness(lines);
    if (!skipped("vn")) numberValues(lines);
    if (std::getenv("CPP11_C6XTRACE")) { std::fprintf(stderr, "--- after numberValues\n"); for (std::size_t i = 0; i < lines.size(); i++) std::fprintf(stderr, "%s\n", lines[i].raw.c_str()); }
    rounds(lines);
    computeLiveness(lines);
    foldAddressing(lines);
    std::string text2 = schedule(lines);
    std::size_t words = 0;
    for (std::size_t i = 0; i < text2.size(); i++) if (text2[i] == '\n') words++;
    return foldBranchNops(text2, words * 4 + 512 < 8192);
}
