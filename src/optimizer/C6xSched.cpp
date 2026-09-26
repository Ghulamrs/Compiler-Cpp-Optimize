// The C6000 passes over one function's text: the -O0 NOPs are dropped, the
// sequential program is rewritten by three peepholes, and it is padded again
// to the hazards that remain. The latencies are the emulator's (its README).

#include "C6xSched.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <vector>

namespace {

struct Line {
    std::string raw;
    bool instr = false;
    std::string pred;                   // "A1" or "!A1", read as A1
    std::string mnem;
    std::vector<std::string> ops;
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

bool blockEnd(const Line &l) { return !l.instr || l.mnem == "B"; }

// Whether reg is overwritten before it is read on the straight path from i;
// a label, a branch or a directive ends the path and answers no.
bool deadAfter(const std::vector<Line> &v, std::size_t i, const std::string &reg) {
    for (std::size_t j = i + 1; j < v.size(); j++) {
        if (blockEnd(v[j])) return false;
        std::vector<std::string> reads, writes;
        readsAndWrites(v[j], reads, writes);
        if (has(reads, reg)) return false;
        if (has(writes, reg)) return true;
    }
    return true;
}

// Whether reg is written before it is read on the straight path from i, the path's end answering no.
bool deadAfterStrict(const std::vector<Line> &v, std::size_t i, const std::string &reg) {
    for (std::size_t j = i + 1; j < v.size(); j++) {
        if (blockEnd(v[j])) return false;
        std::vector<std::string> reads, writes;
        readsAndWrites(v[j], reads, writes);
        if (has(reads, reg)) return false;
        if (has(writes, reg)) return true;
    }
    return false;
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
        if (!deadAfter(v, i + 1, reg)) continue;
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

// An instruction rebuilt from its parts, every operand kept.
Line rebuilt(const std::string &mnem, const std::vector<std::string> &ops) {
    std::string raw = "\t" + mnem;
    for (std::size_t o = 0; o < ops.size(); o++) raw += (o == 0 ? "\t" : ", ") + ops[o];
    return parse(raw);
}

// A register's file: 'A' or 'B', or 0 for anything else.
char sideOf(const std::string &r) { return r.size() >= 2 && (r[0] == 'A' || r[0] == 'B') && std::isdigit(static_cast<unsigned char>(r[1])) ? r[0] : 0; }

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

// **A copy's one reader takes the source itself**, on the straight path and the same register file:
// `MV S, D` then the first reader of D, neither S nor D written between and D dead after it. And the
// other way: an instruction whose result is only copied on writes the copy's destination itself.
bool forwardMoves(std::vector<Line> &v) {
    bool changed = false;
    for (std::size_t i = 0; i < v.size(); i++) {
        const Line &mv = v[i];
        if (mv.mnem != "MV" || mv.ops.size() != 2 || !mv.pred.empty()) continue;
        const std::string S = mv.ops[0], D = mv.ops[1];
        if (!sideOf(S) || sideOf(S) != sideOf(D) || S == D || S == "A15" || D == "A15" || S == "B15" || D == "B15") continue;
        // Retarget: the instruction before made S for this copy alone.
        if (i > 0 && v[i - 1].instr && v[i - 1].pred.empty() && !isStore(v[i - 1].mnem) && v[i - 1].mnem != "B" &&
            v[i - 1].mnem != "MVKH" && !v[i - 1].ops.empty() && v[i - 1].ops.back() == S && deadAfter(v, i, S)) {
            std::vector<std::string> reads, writes;
            readsAndWrites(v[i - 1], reads, writes);
            if (writes.size() == 1 && delaySlots(v[i - 1].mnem) == 0) {
                Line p = v[i - 1];
                p.ops.back() = D;
                v[i - 1] = rebuilt(p.mnem, p.ops);
                v.erase(v.begin() + static_cast<long>(i));
                i--;
                changed = true;
                continue;
            }
        }
        for (std::size_t j = i + 1; j < v.size(); j++) {
            if (blockEnd(v[j])) break;
            std::vector<std::string> reads, writes;
            readsAndWrites(v[j], reads, writes);
            if (has(reads, D)) {
                bool pair = false;          // a register pair is odd:even, and a half cannot be renamed alone
                for (const std::string &op : v[j].ops) if (op.find(':') != std::string::npos) pair = true;
                if (pair || !v[j].pred.empty() || (!has(writes, D) && !deadAfter(v, j, D))) break;
                Line u = v[j];
                const std::size_t last = isStore(u.mnem) || u.mnem == "B" || u.ops.back()[0] == '*' ? u.ops.size() : u.ops.size() - 1;
                for (std::size_t o = 0; o < last; o++) u.ops[o] = renamed(u.ops[o], D, S);
                for (std::size_t o = last; o < u.ops.size(); o++)
                    if (u.ops[o][0] == '*') u.ops[o] = renamed(u.ops[o], D, S);
                v[j] = rebuilt(u.mnem, u.ops);
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
            v[i].ops[1] != "A15" && v[i].ops[1] != "B15" && deadAfterStrict(v, i, v[i].ops[1])) {
            v.erase(v.begin() + static_cast<long>(i));
            i--;
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

class Scheduler {
public:
    void feed(const Line &l);
    std::string finish();
private:
    std::ostringstream out_;
    long cycle_ = 0;                        // the next instruction's issue cycle
    std::map<std::string, long> land_;      // register -> the cycle its pending write lands
    long allLanded() const;
    void waitUntil(long c);
};

long Scheduler::allLanded() const {
    long c = 0;
    for (std::map<std::string, long>::const_iterator it = land_.begin(); it != land_.end(); ++it)
        if (it->second > c) c = it->second;
    return c;
}

void Scheduler::waitUntil(long c) {
    if (c <= cycle_) return;
    out_ << "\tNOP\t" << (c - cycle_) << "\n";
    cycle_ = c;
}

void Scheduler::feed(const Line &l) {
    if (!l.instr) {
        // A label needs no wait, a jump arriving with nothing in flight; a directive is a barrier.
        if (!l.raw.empty() && l.raw[0] == '\t') waitUntil(allLanded());
        out_ << l.raw << "\n";
        return;
    }
    std::vector<std::string> reads, writes;
    readsAndWrites(l, reads, writes);
    int slots = delaySlots(l.mnem);
    long need = cycle_;
    for (std::size_t i = 0; i < reads.size(); i++) {
        std::map<std::string, long>::iterator it = land_.find(reads[i]);
        if (it != land_.end() && it->second > need) need = it->second;
    }
    for (std::size_t i = 0; i < writes.size(); i++) {   // WAW: this write lands after the pending one
        std::map<std::string, long>::iterator it = land_.find(writes[i]);
        if (it != land_.end() && it->second - slots > need) need = it->second - slots;
    }
    if (l.mnem == "B") {                    // the target executes at issue + 6
        long all = allLanded() - 6;
        if (all > need) need = all;
    }
    waitUntil(need);
    out_ << l.raw << "\n";
    long issue = cycle_++;
    for (std::size_t i = 0; i < writes.size(); i++) land_[writes[i]] = issue + slots + 1;
    if (l.mnem == "B") {
        out_ << "\tNOP\t5\n";
        cycle_ += 5;
    }
}

std::string Scheduler::finish() {
    waitUntil(allLanded());
    return out_.str();
}

}   // namespace

std::string c6xSchedule(const std::string &text, int level) {
    if (level <= 0) return text;
    std::vector<Line> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        Line l = parse(line);
        if (l.mnem != "NOP") lines.push_back(l);    // the -O0 padding, re-derived by the Scheduler
    }
    foldMvk(lines);
    foldFrame(lines);
    foldPushPop(lines);
    for (int round = 0; round < 6 && forwardMoves(lines); round++) {}
    Scheduler s;
    for (std::size_t i = 0; i < lines.size(); i++) s.feed(lines[i]);
    return s.finish();
}
