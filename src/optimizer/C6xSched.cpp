// The re-padding pass for the C6000: every instruction waits only as long as
// the writes still in flight require, and a branch waits until they land by
// the time its target executes. The latencies are the emulator's (README).
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
        // A label needs no wait: a jump arrives with nothing in flight, and the
        // fall-through path is scheduled as the sequence it is. A directive is
        // a barrier, since what follows it is not known to be code.
        if (!l.raw.empty() && l.raw[0] == '\t') waitUntil(allLanded());
        out_ << l.raw << "\n";
        return;
    }
    if (l.mnem == "NOP") return;            // the -O0 padding, re-derived below
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
    Scheduler s;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) s.feed(parse(line));
    return s.finish();
}
