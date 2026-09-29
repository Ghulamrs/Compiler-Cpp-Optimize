// The C6000 passes over one function's text: the -O0 NOPs are dropped, the
// sequential program is rewritten by the peepholes under a liveness of its
// blocks, and each block is list-scheduled into execute packets, the delay
// slots of its branch filled, and padded to the hazards that remain.

#include "C6xSched.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace {

struct Line {
    std::string raw;
    bool instr = false;
    std::string pred;                   // "A1" or "!A1", read as A1
    std::string mnem;
    std::vector<std::string> ops;
    std::uint64_t liveOut = ~0ull;      // the registers live at the end of this line's block
};

bool startsWith(const std::string &s, const char *p) { return s.compare(0, std::string(p).size(), p) == 0; }
bool endsWith(const std::string &s, const char *p) {
    std::string q(p);
    return s.size() >= q.size() && s.compare(s.size() - q.size(), q.size(), q) == 0;
}

// Delay slots by mnemonic, as the -O0 backend pads them: the unknown floating
// forms take the longest of their precision rather than none.
int delaySlots(const std::string &m) {
    if (m == "B") return 5;
    if (startsWith(m, "LD")) return 4;
    if (m == "MPYDP") return 9;
    if (m == "ADDDP" || m == "SUBDP") return 6;
    if (m == "INTDP" || m == "INTDPU") return 4;
    if (startsWith(m, "CMP") && (endsWith(m, "DP") || endsWith(m, "SP"))) return 1;
    if (m == "SPDP" || m == "DPSP") return 1;
    if (m == "MPYSP" || m == "ADDSP" || m == "SUBSP") return 3;
    if (m == "INTSP" || m == "INTSPU" || m == "SPTRUNC" || m == "DPTRUNC" || m == "SPINT" || m == "DPINT") return 3;
    if (startsWith(m, "MPY")) return 3;
    if (endsWith(m, "DP")) return 9;
    if (endsWith(m, "SP")) return 3;
    return 0;
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

Line make(const std::string &mnem, const std::string &a, const std::string &b = "", const std::string &c = "") {
    std::string raw = "\t" + mnem + "\t" + a;
    if (!b.empty()) raw += ", " + b;
    if (!c.empty()) raw += ", " + c;
    return parse(raw);
}

// An instruction rebuilt from its parts, every operand kept, and the predicate with it.
Line rebuilt(const std::string &mnem, const std::vector<std::string> &ops, const std::string &pred = "") {
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
    if (!hasDest) return;
    registersIn(l.ops.back(), writes);
    if (l.mnem == "MVKH") registersIn(l.ops.back(), reads);
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
bool blockEnd(const Line &l) { return !l.instr || l.mnem == "B"; }

// ----- the calls, the returns and what they read and write -----

const char *const kArgRegs[] = { "A4", "A5", "B4", "B5", "A6", "A7", "B6", "B7", "A8", "A9", "B8", "B9",
                                 "A10", "A11", "B10", "B11", "A12", "A13", "B12", "B13", "B3", "A15", "B15", "B14" };
const char *const kExitRegs[] = { "A4", "A5", "A10", "A11", "A12", "A13", "A14", "A15", "B3", "B10", "B11", "B12", "B13", "B14", "B15" };

// The registers a call reads (the EABI's argument registers, B3, the stack pointers) and clobbers.
std::uint64_t callReads() { return maskOf(kArgRegs, sizeof kArgRegs / sizeof kArgRegs[0]); }
std::uint64_t callWrites() {
    std::uint64_t m = 0;
    for (int n = 0; n < 32; n++) if (n <= 9 || n >= 16) m |= (1ull << n) | (1ull << (n + 32));
    return m;
}
std::uint64_t exitLive() { return maskOf(kExitRegs, sizeof kExitRegs / sizeof kExitRegs[0]); }

// A branch to a symbol that is no label of this text, or through a register other than B3, is a call.
bool isCall(const Line &l, const std::set<std::string> &labels) {
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
            if (l.mnem == "B") {
                if (isCall(l, labels)) { r |= callReads(); w = callWrites(); }
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
            if (unknown[b]) o = ~0ull;
            for (std::size_t k = 0; k < succ[b].size(); k++) o |= in[succ[b][k]];
            std::uint64_t n = use[b] | (o & ~def[b]);
            if (o != out[b] || n != in[b]) { out[b] = o; in[b] = n; changed = true; }
        }
    }
    for (std::size_t b = 0; b < nb; b++) {
        std::size_t end = b + 1 < nb ? starts[b + 1] : v.size();
        for (std::size_t i = starts[b]; i < end; i++) v[i].liveOut = out[b];
    }
}

// Whether reg is overwritten before it is read on the straight path from i, or
// is not live where the block ends; a directive ends the path and answers no.
bool deadAfter(const std::vector<Line> &v, std::size_t i, const std::string &reg) {
    for (std::size_t j = i + 1; j < v.size(); j++) {
        if (!v[j].instr) return isLabel(v[j]) ? (v[i].liveOut & bitOf(reg)) == 0 : false;
        std::vector<std::string> reads, writes;
        readsAndWrites(v[j], reads, writes);
        if (has(reads, reg)) return false;
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
    for (std::size_t i = 0; i + 1 < v.size(); i++) {
        if (v[i].mnem != "SUB" || v[i].ops.size() != 3 || v[i].ops[0] != "A15") continue;
        std::string reg = v[i].ops[2];
        long k;
        std::size_t first = i;
        if (isNumber(v[i].ops[1])) k = std::atol(v[i].ops[1].c_str());
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

// How many of an instruction's source registers sit on the other side from
// its destination - at most one may, over the cross path; a store's data and
// a load's destination travel a data path instead and are not counted.
int crossings(const Line &l) {
    if (l.ops.empty() || isStore(l.mnem) || isLoad(l.mnem) || l.mnem == "B") return 0;
    std::vector<std::string> dst;
    registersIn(l.ops.back(), dst);
    if (dst.empty()) return 0;
    int n = 0;
    for (std::size_t o = 0; o + 1 < l.ops.size(); o++) {
        std::vector<std::string> regs;
        registersIn(l.ops[o], regs);
        bool crossed = false;
        for (std::size_t k = 0; k < regs.size(); k++) if (sideOf(regs[k]) != sideOf(dst[0])) crossed = true;
        if (crossed && regs.size() > 1) return 2;     // a pair never crosses
        if (crossed) n++;
    }
    return n;
}

// A register-to-register instruction whose operands may be renamed: no
// memory operand, and no pair - a half of A5:A4 cannot be renamed alone.
bool renameable(const Line &l) {
    if (!l.instr || l.mnem == "B" || isStore(l.mnem) || isLoad(l.mnem)) return false;
    for (std::size_t o = 0; o < l.ops.size(); o++) if (l.ops[o].find(':') != std::string::npos || l.ops[o][0] == '*') return false;
    return true;
}

// **A copy's one reader takes the source itself**, on the straight path: `MV S, D` then the first
// reader of D, neither S nor D written between and D dead after it - across the register files too,
// where the reader is left with one crossing source. And the other way: an instruction whose result
// is only copied on writes the copy's destination itself.
bool forwardMoves(std::vector<Line> &v) {
    bool changed = false;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &mv = v[i];
        if (mv.mnem != "MV" || mv.ops.size() != 2 || !mv.pred.empty()) continue;
        const std::string S = mv.ops[0], D = mv.ops[1];
        if (!sideOf(S) || !sideOf(D) || S == D || S == "A15" || D == "A15" || S == "B15" || D == "B15") continue;
        // Retarget: the instruction before made S for this copy alone.
        if (i > 0 && renameable(v[i - 1]) && v[i - 1].pred.empty() && v[i - 1].mnem != "MVKH" &&
            !v[i - 1].ops.empty() && v[i - 1].ops.back() == S && deadAfter(v, i, S)) {
            std::vector<std::string> reads, writes;
            readsAndWrites(v[i - 1], reads, writes);
            if (writes.size() == 1 && delaySlots(v[i - 1].mnem) == 0) {
                Line p = v[i - 1];
                p.ops.back() = D;
                Line r = rebuilt(p.mnem, p.ops);
                if (crossings(r) <= 1) {
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
                if (memSide || crossings(r) > 1 || (r.mnem == "B" && sideOf(r.ops[0]) == 'A')) break;
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

// **A small constant goes into its one reader**: `MVK k, R` then the first reader of R, R
// dead after, where the reader has a constant form - ADD, AND, OR, XOR and the compares
// take -16..15 in either place, SUB r,k takes 0..31, a shift takes a count of 0..31.
bool forwardConstants(std::vector<Line> &v) {
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

bool isPush(const std::vector<Line> &v, std::size_t i) {
    return i + 1 < v.size() && v[i].raw == "\tSUB\tB15, 8, B15" && isStore(v[i + 1].mnem) &&
           v[i + 1].ops.size() == 2 && v[i + 1].ops[1] == "*B15" && (v[i + 1].mnem == "STW" || v[i + 1].mnem == "STDW");
}
bool isPop(const std::vector<Line> &v, std::size_t i) {
    return i + 1 < v.size() && (v[i].mnem == "LDW" || v[i].mnem == "LDDW") && v[i].ops.size() == 2 &&
           v[i].ops[0] == "*B15" && v[i + 1].raw == "\tADD\tB15, 8, B15";
}

// A push whose pop is in the same block, with no call and no other use of
// B15 between, keeps its value in A16-A31 instead: the pair's slot d takes
// A(16+2d):A(17+2d), and the two stack adjustments go with the memory access.
void foldPushPop(std::vector<Line> &v) {
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
        if (depth >= 8) continue;
        std::string lo = "A" + std::to_string(16 + 2 * depth), hi = "A" + std::to_string(17 + 2 * depth);
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

enum { UL = 1, US = 2, UD = 4, UM = 8 };

// One instruction as the scheduler sees it: what it reads and writes, its
// latency, the units it has a form on, its side and cross path, its memory
// access, and the edges from the earlier instructions it must follow.
struct Node {
    Line line;
    std::uint64_t reads = 0, writes = 0;
    int lat = 1;
    unsigned units = 0;                 // a bitmask of UL, US, UD, UM; 0 for a form the table does not know
    int side = -1;                      // 0 A, 1 B, -1 either (a branch to a label)
    int cross = -1;                     // the cross path used, by the side that reads: 0 1X, 1 2X, -1 none
    bool mem = false, store = false;
    int tpath = -1;                     // a load's or store's data path: the data register's side
    std::string memBase; long memOff = 0; int memSize = 0; bool memFrame = false;
    bool branch = false, call = false;
    std::vector<std::pair<int, int> > preds;   // (earlier node, the least distance in cycles)
    int issue = -1, height = 0;
};

// The units a mnemonic has a form on for these operands, from asm6x's table
// (ASM6x src/forms.h): the three-operand arithmetic on any of .L .S .D with a
// register, the small constants on .L and .S, the wider ones on .D alone.
unsigned unitsFor(const Line &l) {
    const std::string &m = l.mnem;
    if (m == "B") return US;
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
    if (m == "SUB") {
        if (pair) return UL;
        if (at < 0) return UL | US | UD;
        if (at == 0 && small) return UL | US;
        if (at == 1 && ucst && crossings(l) == 0) return UD;
        return 0;
    }
    if (m == "NOT") return UL | US | UD;
    if (m == "NEG" || m == "MV") return pair ? UL : UL | US | UD;
    if (m == "ZERO") return UL;
    if (m == "CLR" || m == "EXT" || m == "EXTU" || m == "SHL" || m == "SHR" || m == "SHRU") return US;
    if (m == "MVK") return isNumber(l.ops[0]) && std::atol(l.ops[0].c_str()) >= -16 && std::atol(l.ops[0].c_str()) <= 15 ? UL | US : US;
    if (m == "MVKL" || m == "MVKH") return US;
    if (m == "CMPEQ" || m == "CMPGT" || m == "CMPLT" || m == "CMPGTU" || m == "CMPLTU") return UL;
    if (endsWith(m, "SP") || endsWith(m, "DP")) {
        if (startsWith(m, "CMP") || m == "SPDP") return US;
        if (m == "ADDSP" || m == "SUBSP" || m == "ADDDP" || m == "SUBDP") return UL | US;
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
    n.units = unitsFor(l);
    if (l.mnem == "B") {
        n.branch = true;
        n.call = isCall(l, labels);
        if (n.call) { n.reads |= callReads(); n.writes |= callWrites(); }
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

// The packet being filled at one cycle: the units taken on each side, the
// cross paths, the data paths, the registers written and the branch.
struct Packet {
    bool unit[2][4] = { { false, false, false, false }, { false, false, false, false } };
    bool cross[2] = { false, false }, tpath[2] = { false, false };
    std::uint64_t writes = 0;
    bool branch = false, alone = false;
    int count = 0;
    std::vector<const Node *> members;
};

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
    bool taken[2][4] = { { false, false, false, false }, { false, false, false, false } };
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

// One block - the instructions between two labels, ending in its branch if it
// has one - scheduled by list scheduling: the edges between instructions are
// the register and memory orders with their distances in cycles, each cycle
// takes the ready instructions of greatest height that its packet has units
// for, and the branch goes as early as leaves every other instruction issued
// in its window and landed by the time its target runs.
void scheduleBlock(std::vector<Node> &nodes, std::ostringstream &out) {
    const std::size_t n = nodes.size();
    int branchAt = -1;
    for (std::size_t i = 0; i < n; i++) if (nodes[i].branch) branchAt = static_cast<int>(i);
    for (std::size_t i = 0; i < n; i++) {
        Node &b = nodes[i];
        for (std::size_t j = 0; j < i; j++) {
            const Node &a = nodes[j];
            int d = -1;
            if (a.writes & b.reads) d = std::max(d, a.lat);
            if (a.reads & b.writes) d = std::max(d, a.store ? 1 : 0);
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
        cycles.push_back(Packet());
        Packet &p = cycles.back();
        for (;;) {
            int best = -1;
            for (std::size_t i = 0; i < n; i++) {
                Node &x = nodes[i];
                if (x.issue >= 0 || x.branch) continue;
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
            nodes[static_cast<std::size_t>(best)].issue = c;
            add(p, nodes[static_cast<std::size_t>(best)]);
            left--;
        }
    }
    int length = 0;
    for (std::size_t i = 0; i < n; i++) if (!nodes[i].branch) length = std::max(length, nodes[i].issue + nodes[i].lat);
    if (branchAt >= 0) {
        Node &b = nodes[static_cast<std::size_t>(branchAt)];
        int at = 0;
        for (std::size_t e = 0; e < b.preds.size(); e++) {
            const Node &y = nodes[static_cast<std::size_t>(b.preds[e].first)];
            at = std::max(at, y.issue + b.preds[e].second);
        }
        for (std::size_t i = 0; i < n; i++) {
            if (nodes[i].branch) continue;
            at = std::max(at, nodes[i].issue - 5);                 // issued in the window
            at = std::max(at, nodes[i].issue + nodes[i].lat - 6);  // landed when the target runs
        }
        for (;;) {
            while (static_cast<int>(cycles.size()) <= at) cycles.push_back(Packet());
            if (fits(cycles[static_cast<std::size_t>(at)], b)) break;
            at++;
        }
        b.issue = at;
        add(cycles[static_cast<std::size_t>(at)], b);
        length = at + 6;
    }
    int nops = 0;
    for (int c = 0; c < length; c++) {
        const Packet *p = c < static_cast<int>(cycles.size()) ? &cycles[static_cast<std::size_t>(c)] : nullptr;
        if (!p || p->count == 0) { nops++; continue; }
        while (nops > 0) { out << "\tNOP\t" << std::min(nops, 9) << "\n"; nops -= std::min(nops, 9); }
        for (std::size_t k = 0; k < p->members.size(); k++) out << (k == 0 ? "" : "||") << p->members[k]->line.raw << "\n";
    }
    while (nops > 0) { out << "\tNOP\t" << std::min(nops, 9) << "\n"; nops -= std::min(nops, 9); }
}

// The whole text: each block scheduled on its own, every label and directive kept where it is.
std::string schedule(const std::vector<Line> &v) {
    std::set<std::string> labels;
    for (std::size_t i = 0; i < v.size(); i++) if (isLabel(v[i])) labels.insert(labelName(v[i]));
    std::ostringstream out;
    std::vector<Node> block;
    auto flush = [&]() { if (!block.empty()) scheduleBlock(block, out); block.clear(); };
    for (std::size_t i = 0; i < v.size(); i++) {
        if (!v[i].instr) { flush(); out << v[i].raw << "\n"; continue; }
        block.push_back(makeNode(v[i], labels));
        if (v[i].mnem == "B") flush();
    }
    flush();
    return out.str();
}

}   // namespace

std::string c6xSchedule(const std::string &text, int level) {
    if (level <= 0) return text;
    std::vector<Line> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        Line l = parse(line);
        if (l.mnem != "NOP") lines.push_back(l);    // the -O0 padding, re-derived by the scheduler
    }
    foldMvk(lines);
    computeLiveness(lines);
    foldFrame(lines);
    foldPushPop(lines);
    for (int round = 0; round < 8; round++) {
        computeLiveness(lines);
        bool changed = forwardMoves(lines);
        computeLiveness(lines);
        changed = forwardConstants(lines) || changed;
        computeLiveness(lines);
        changed = flipPredicates(lines) || changed;
        if (!changed) break;
    }
    return schedule(lines);
}
