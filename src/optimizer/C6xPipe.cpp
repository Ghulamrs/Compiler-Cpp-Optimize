// Software pipelining of the innermost counted loops of one function's C6000 text at -O2: a loop that is one block
// ending in `[P] B head`, counted by `ADD 1, i, i` against a bound, is modulo-scheduled at the least II its dependences
// and units allow, renamed per iteration where a value outlives II, and emitted as prologue, kernel and epilogue.

#include "C6xModel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

namespace c6x {
namespace {

int tracing() {
    static int t = -1;
    if (t < 0) t = std::getenv("CPP11_PIPE") ? std::atoi(std::getenv("CPP11_PIPE")) : 0;
    return t;
}

// ----- addresses as linear forms: term -> coefficient, "" the constant, "iv" the induction variable -----

typedef std::map<std::string, long> Form;

Form formOf(const std::string &term, long k = 1) { Form f; f[term] = k; return f; }
Form combine(const Form &a, const Form &b, long sign) {
    Form r = a;
    for (Form::const_iterator it = b.begin(); it != b.end(); ++it) r[it->first] += sign * it->second;
    for (Form::iterator it = r.begin(); it != r.end();) { if (it->second == 0) r.erase(it++); else ++it; }
    return r;
}
Form scaled(const Form &a, long k) {
    Form r;
    if (k != 0) for (Form::const_iterator it = a.begin(); it != a.end(); ++it) r[it->first] = it->second * k;
    return r;
}
bool isConstForm(const Form &f, long &k) {
    k = 0;
    if (f.empty()) return true;
    if (f.size() == 1 && f.begin()->first == "") { k = f.begin()->second; return true; }
    return false;
}
// The one object a form is based on - a global symbol or the frame, with coefficient one - or "" if none or several.
std::string objectOf(const Form &f) {
    std::string o;
    for (Form::const_iterator it = f.begin(); it != f.end(); ++it) {
        if (it->first.empty() || it->first == "iv" || it->first.compare(0, 4, "reg:") == 0) continue;
        if (!o.empty() || it->second != 1) return "";
        o = it->first;
    }
    return o;
}

struct Access { bool known = false; Form addr; };

// What one line makes of its destination - a form, or nothing known - entered in val, the rest of its writes lost.
void evalLine(const Line &l, const std::vector<std::string> &writes, std::map<std::string, Form> &val, std::set<std::string> &lost) {
    if (writes.empty()) return;
    const std::string &d = writes.back();
    Form nv; bool knownV = false; long k = 0, k2 = 0;
    const std::vector<std::string> &p = l.ops;
    bool cst0 = p.size() > 1 && isNumber(p[0]), cst1 = p.size() > 2 && isNumber(p[1]);
    Form v0 = p.size() > 1 && val.count(p[0]) ? val[p[0]] : Form(), v1 = p.size() > 2 && val.count(p[1]) ? val[p[1]] : Form();
    bool k0 = cst0 || (p.size() > 1 && !v0.empty()), k1 = cst1 || (p.size() > 2 && !v1.empty());
    if (cst0) v0 = formOf("", std::atol(p[0].c_str()));
    if (cst1) v1 = formOf("", std::atol(p[1].c_str()));
    if (l.pred.empty() && writes.size() == 1) {
        if (l.mnem == "MVK" && cst0) { nv = v0; knownV = true; }
        else if (l.mnem == "MVKL" && p.size() == 2) { nv = cst0 ? v0 : formOf("@" + p[0]); knownV = true; }
        else if (l.mnem == "MVKH" && p.size() == 2 && val.count(d) && val[d] == (cst0 ? v0 : formOf("@" + p[0]))) { nv = val[d]; knownV = true; }
        else if (l.mnem == "MV" && k0) { nv = v0; knownV = true; }
        else if (l.mnem == "ADD" && k0 && k1) { nv = combine(v0, v1, 1); knownV = true; }
        else if (l.mnem == "SUB" && k0 && k1) { nv = combine(v0, v1, -1); knownV = true; }
        else if (l.mnem == "SUBAW" && k0 && cst1) { nv = combine(v0, scaled(v1, 4), -1); knownV = true; }
        else if (l.mnem == "ADDK" && cst0 && val.count(d)) { nv = combine(val[d], v0, 1); knownV = true; }
        else if (l.mnem == "SHL" && k0 && cst1 && isConstForm(v1, k) && k >= 0 && k < 31) { nv = scaled(v0, 1L << k); knownV = true; }
        else if (l.mnem == "MPY32" && k0 && k1 && (isConstForm(v1, k2) || isConstForm(v0, k))) { nv = isConstForm(v1, k2) ? scaled(v0, k2) : scaled(v1, k); knownV = true; }
    }
    for (std::size_t w = 0; w < writes.size(); w++) { val.erase(writes[w]); lost.insert(writes[w]); }
    if (knownV && !nv.empty()) { val[d] = nv; lost.erase(d); }
}

// The address forms of the body's memory operands, from a walk over the body with the induction variable as
// `iv`, a register the loop never writes as its own term, and a register written before its first read unknown.
std::vector<Access> addressForms(const std::vector<Line> &body, const std::vector<Line> &pre, const std::string &iv, const std::set<std::string> &written) {
    std::map<std::string, Form> val, before;
    std::set<std::string> lost;
    for (std::size_t o = 0; o < pre.size(); o++) {           // what the preheader computed, by the same rules
        std::vector<std::string> reads, writes;
        readsAndWrites(pre[o], reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) if (!before.count(reads[r]) && !lost.count(reads[r])) before[reads[r]] = formOf(reads[r] == "A15" ? "A15" : "reg:" + reads[r]);
        evalLine(pre[o], writes, before, lost);
    }
    lost.clear();
    std::vector<Access> out(body.size());
    for (std::size_t o = 0; o < body.size(); o++) {
        const Line &l = body[o];
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++)
            if (!val.count(reads[r]) && !lost.count(reads[r]))
                val[reads[r]] = reads[r] == iv ? formOf("iv") : written.count(reads[r]) ? Form() : before.count(reads[r]) ? before[reads[r]] : formOf(reads[r] == "A15" ? "A15" : "reg:" + reads[r]);
        if (isLoad(l.mnem) || isStore(l.mnem)) {
            const std::string &m = isStore(l.mnem) ? l.ops[1] : l.ops[0];
            std::string base; long off = 0; std::string index; long sign = 1;
            if (m.size() > 1 && m[0] == '*' && (m[1] == '+' || m[1] == '-') && m.find('(') != std::string::npos) {
                sign = m[1] == '-' ? -1 : 1;
                base = m.substr(2, m.find('(') - 2);
                off = std::atol(m.substr(m.find('(') + 1).c_str());
            } else if (m.size() > 1 && m[0] == '*' && (m[1] == '+' || m[1] == '-') && m.find('[') != std::string::npos) {
                sign = m[1] == '-' ? -1 : 1;
                base = m.substr(2, m.find('[') - 2);
                index = m.substr(m.find('[') + 1, m.find(']') - m.find('[') - 1);
            } else if (m.size() > 1 && m[0] == '*' && sideOf(m.substr(1))) base = m.substr(1);
            bool known = !base.empty() && val.count(base) && !val[base].empty();
            if (known && !index.empty()) known = val.count(index) && !val[index].empty();
            if (known) {
                Form a = val[base];
                if (!index.empty()) a = combine(a, scaled(val[index], accessSize(l.mnem)), sign);
                else if (off) a = combine(a, formOf("", off), sign);
                out[o].known = true; out[o].addr = a;
            }
        }
        evalLine(l, writes, val, lost);
    }
    return out;
}

// Whether access a, in iteration j, and b, in iteration j + d, may touch the same bytes.
bool mayAliasAt(const Access &a, int sa, const Access &b, int sb, int d) {
    if (!a.known || !b.known) return true;
    Form diff = combine(b.addr, a.addr, -1);
    Form::const_iterator iv = b.addr.find("iv");
    if (iv != b.addr.end()) diff = combine(diff, formOf("", d * iv->second), 1);
    long c;
    if (isConstForm(diff, c)) return c > -sb && c < sa;
    std::string oa = objectOf(a.addr), ob = objectOf(b.addr);
    return oa.empty() || ob.empty() || oa == ob;
}

// ----- the loop as recognised -----

struct Edge { int from, to, delay, dist; const char *why; };

struct Loop {
    std::size_t head = 0, back = 0;
    std::string label, exitName, P, C, iv, bound;
    bool negated = false, boundConst = false, plusOne = false;
    long boundK = 0;
    std::size_t cmp = 0;
    std::vector<std::size_t> bodyAt;    // the body's instruction lines, the compare and the branch left out
    std::vector<Line> body, pre;          // the body, and the block that falls into the head
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::set<std::string> used, written, predicated, rmwFirst;
    std::uint64_t liveExit = 0;
};

const char *const kCond[] = { "A0", "A1", "A2", "B0", "B1", "B2" };

bool recognise(const std::vector<Line> &v, std::size_t back, const std::set<std::string> &labels, const std::set<std::string> &named, Loop &L, std::string &why) {
    const Line &b = v[back];
    if (b.pred.empty() || labels.count(b.ops[0]) == 0) { why = "no conditional branch to a label"; return false; }
    L.label = b.ops[0];
    L.P = b.pred[0] == '!' ? b.pred.substr(1) : b.pred;
    L.negated = b.pred[0] == '!';
    std::size_t h = 0; bool found = false;
    for (std::size_t i = 0; i < back; i++) if (isLabel(v[i]) && labelName(v[i]) == L.label) { h = i; found = true; }
    if (!found) { why = "the head is not above"; return false; }
    if (back + 1 >= v.size() || !isLabel(v[back + 1])) { why = "no exit label after the branch"; return false; }
    L.head = h; L.back = back; L.exitName = labelName(v[back + 1]);
    for (std::size_t k = h; k-- > 0 && v[k].instr && !isBranch(v[k].mnem);) L.pre.insert(L.pre.begin(), v[k]);
    int names = 0;
    for (std::size_t i = 0; i < v.size(); i++) if (v[i].instr) for (std::size_t o = 0; o < v[i].ops.size(); o++) if (v[i].ops[o] == L.label) names++;
    if (names != 1) { why = "the head is entered from elsewhere"; return false; }
    for (std::size_t i = h + 1; i < back; i++) {
        const Line &l = v[i];
        if (isLabel(l)) { if (!passThrough(l, named)) { why = "a label inside"; return false; } continue; }
        if (!l.instr) { why = "a directive inside"; return false; }
        if (isBranch(l.mnem)) { why = "a branch inside"; return false; }
        if (unitsFor(l) == 0) { why = "an instruction of no known unit: " + l.mnem; return false; }
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        if (has(writes, "A15") || has(writes, "B15")) { why = "the stack pointer is written"; return false; }
        L.bodyAt.push_back(i);
    }
    if (L.bodyAt.size() < 2 || L.bodyAt.size() > 64) { why = "too small or too large"; return false; }
    // The compare: the last writer of P, of a form the count can be read from.
    bool cmpFound = false;
    for (std::size_t k = L.bodyAt.size(); k-- > 0;) {
        std::vector<std::string> reads, writes;
        readsAndWrites(v[L.bodyAt[k]], reads, writes);
        if (has(writes, L.P)) { L.cmp = L.bodyAt[k]; cmpFound = true; break; }
    }
    if (!cmpFound) { why = "the predicate is not written in the loop"; return false; }
    const Line &c = v[L.cmp];
    bool lt = c.mnem == "CMPLT" || c.mnem == "CMPLTU", gt = c.mnem == "CMPGT" || c.mnem == "CMPGTU";
    if ((!lt && !gt) || c.ops.size() != 3 || !c.pred.empty() || c.ops[2] != L.P) { why = "the test is not a compare of two"; return false; }
    // Loops while i < n: [P] with i < n or n > i; while i <= n: [!P] with i > n or n < i.
    if (lt == !L.negated) { L.iv = c.ops[0]; L.bound = c.ops[1]; }
    else { L.iv = c.ops[1]; L.bound = c.ops[0]; }
    L.plusOne = L.negated;
    if (isNumber(L.bound)) { L.boundConst = true; L.boundK = std::atol(L.bound.c_str()); }
    else if (!sideOf(L.bound)) { why = "the bound is not a register"; return false; }
    if (!sideOf(L.iv) || (L.boundConst && L.iv != c.ops[1])) { why = "the counter is not a register"; return false; }
    // P is read by the branch alone and is dead at the exit; i is stepped once, by one, before the test.
    L.liveExit = v[back + 1].liveIn;
    if (L.liveExit & bitOf(L.P)) { why = "the predicate lives past the loop"; return false; }
    int ivWrites = 0, boundWrites = 0, pWrites = 0;
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const Line &l = v[L.bodyAt[k]];
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        if (has(reads, L.P) && pWrites == 0) { why = "the predicate's test is read in the loop"; return false; }
        if (has(writes, L.iv)) {
            ivWrites++;
            bool step = l.mnem == "ADD" && l.pred.empty() && l.ops.size() == 3 && l.ops[2] == L.iv &&
                        ((l.ops[0] == "1" && l.ops[1] == L.iv) || (l.ops[1] == "1" && l.ops[0] == L.iv));
            if (!step || L.bodyAt[k] > L.cmp) { why = "the counter is not stepped by one before the test"; return false; }
        }
        if (!isNumber(L.bound) && has(writes, L.bound) && L.bodyAt[k] < L.cmp) {
            boundWrites++;
            if (l.mnem == "MVK" && l.pred.empty() && l.ops.size() == 2 && isNumber(l.ops[0])) { L.boundConst = true; L.boundK = std::atol(l.ops[0].c_str()); }
            else L.boundConst = false;
        }
        if (has(writes, L.P) && L.bodyAt[k] != L.cmp) pWrites++;
        for (std::size_t r = 0; r < reads.size(); r++) L.used.insert(reads[r]);
        for (std::size_t w = 0; w < writes.size(); w++) { L.used.insert(writes[w]); L.written.insert(writes[w]); if (!l.pred.empty()) L.predicated.insert(writes[w]); }
    }
    if (ivWrites != 1) { why = "the counter is stepped " + std::to_string(ivWrites) + " times"; return false; }
    if (boundWrites > 0 && !L.boundConst) { why = "the bound is written in the loop"; return false; }
    // The kernel's counter: P itself where the compare alone writes it, else a condition register the loop leaves alone.
    if (pWrites == 0) L.C = L.P;
    else for (std::size_t k = 0; k < 6 && L.C.empty(); k++)
        if (!L.used.count(kCond[k]) && !(L.liveExit & bitOf(kCond[k])) && kCond[k] != L.P) L.C = kCond[k];
    if (L.C.empty()) { why = "no condition register for the count"; return false; }
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const Line &l = v[L.bodyAt[k]];
        if (L.bodyAt[k] != L.cmp && !(l.mnem == "MV" && l.ops.size() == 2 && l.ops[0] == l.ops[1])) L.body.push_back(l);
    }
    return true;
}

int antiDelay(const Node &r, const Node &w) { return std::max(r.store ? 1 : 0, r.late - w.lat + 1); }

// The dependences of one iteration and the next: register flow, anti and output orders in program order, the
// same wrapped round the back edge - the anti and output ones only for a register kept under one name - and memory.
void buildEdges(Loop &L, const std::set<std::string> &renameable) {
    std::vector<Node> &n = L.nodes;
    L.edges.clear();
    std::map<std::string, std::vector<std::pair<int, bool> > > acc;     // register -> (op, isWrite) in order
    for (std::size_t o = 0; o < n.size(); o++) {
        std::vector<std::string> reads, writes;
        readsAndWrites(n[o].line, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) acc[reads[r]].push_back(std::make_pair(static_cast<int>(o), false));
        for (std::size_t w = 0; w < writes.size(); w++) acc[writes[w]].push_back(std::make_pair(static_cast<int>(o), true));
    }
    for (std::map<std::string, std::vector<std::pair<int, bool> > >::iterator it = acc.begin(); it != acc.end(); ++it) {
        const std::vector<std::pair<int, bool> > &a = it->second;
        int firstW = -1, lastW = -1;
        for (std::size_t k = 0; k < a.size(); k++) if (a[k].second) { if (firstW < 0) firstW = static_cast<int>(k); lastW = static_cast<int>(k); }
        if (firstW < 0) continue;
        for (std::size_t k = 0; k < a.size(); k++) {
            int prevW = -1, nextW = -1;
            for (int m = static_cast<int>(k) - 1; m >= 0; m--) if (a[static_cast<std::size_t>(m)].second) { prevW = m; break; }
            for (std::size_t m = k + 1; m < a.size(); m++) if (a[m].second) { nextW = static_cast<int>(m); break; }
            const int op = a[k].first;
            if (!a[k].second) {
                if (prevW >= 0 && a[static_cast<std::size_t>(prevW)].first != op) { Edge e = { a[static_cast<std::size_t>(prevW)].first, op, n[static_cast<std::size_t>(a[static_cast<std::size_t>(prevW)].first)].lat, 0, "flow" }; L.edges.push_back(e); }
                if (prevW < 0) { Edge e = { a[static_cast<std::size_t>(lastW)].first, op, n[static_cast<std::size_t>(a[static_cast<std::size_t>(lastW)].first)].lat, 1, "flow1" }; L.edges.push_back(e); }
                if (nextW >= 0 && a[static_cast<std::size_t>(nextW)].first != op && (prevW >= 0 || !renameable.count(it->first))) { const Node &w = n[static_cast<std::size_t>(a[static_cast<std::size_t>(nextW)].first)]; Edge e = { op, a[static_cast<std::size_t>(nextW)].first, antiDelay(n[static_cast<std::size_t>(op)], w), 0, "anti" }; L.edges.push_back(e); }
                if (nextW < 0 && !renameable.count(it->first)) { const Node &w = n[static_cast<std::size_t>(a[static_cast<std::size_t>(firstW)].first)]; Edge e = { op, a[static_cast<std::size_t>(firstW)].first, antiDelay(n[static_cast<std::size_t>(op)], w), 1, "anti1" }; L.edges.push_back(e); }
            } else if (nextW >= 0) {
                const Node &w1 = n[static_cast<std::size_t>(op)], &w2 = n[static_cast<std::size_t>(a[static_cast<std::size_t>(nextW)].first)];
                Edge e = { op, a[static_cast<std::size_t>(nextW)].first, std::max(1, w1.lat - w2.lat + 1), 0, "out" }; L.edges.push_back(e);
            } else if (!renameable.count(it->first)) {
                const Node &w1 = n[static_cast<std::size_t>(op)], &w2 = n[static_cast<std::size_t>(a[static_cast<std::size_t>(firstW)].first)];
                Edge e = { op, a[static_cast<std::size_t>(firstW)].first, std::max(1, w1.lat - w2.lat + 1), 1, "out1" }; L.edges.push_back(e);
            }
        }
    }
    if (tracing() > 3) for (std::size_t e = 0; e < L.edges.size(); e++) std::fprintf(stderr, "  edge %s %d->%d (%d,%d)\n", L.edges[e].why, L.edges[e].from, L.edges[e].to, L.edges[e].delay, L.edges[e].dist);
    std::vector<Access> addr = addressForms(L.body, L.pre, L.iv, L.written);
    for (std::size_t a = 0; a < n.size(); a++) {
        if (!n[a].mem) continue;
        for (std::size_t b = a; b < n.size(); b++) {
            if (!n[b].mem || (!n[a].store && !n[b].store)) continue;
            if (a != b && mayAliasAt(addr[a], n[a].memSize, addr[b], n[b].memSize, 0)) { Edge e = { static_cast<int>(a), static_cast<int>(b), 1, 0, "mem" }; L.edges.push_back(e); }
            if (mayAliasAt(addr[a], n[a].memSize, addr[b], n[b].memSize, 1)) { Edge e = { static_cast<int>(a), static_cast<int>(b), 1, 1, "mem1" }; L.edges.push_back(e); }
            if (a != b && mayAliasAt(addr[b], n[b].memSize, addr[a], n[a].memSize, 1)) { Edge e = { static_cast<int>(b), static_cast<int>(a), 1, 1, "mem1r" }; L.edges.push_back(e); }
        }
    }
}

// The longest path through one iteration, as a list schedule would take at least - what pipelining is measured against.
int criticalPath(const Loop &L) {
    std::vector<int> t(L.nodes.size(), 0);
    int len = 0;
    for (std::size_t o = 0; o < L.nodes.size(); o++) {
        for (std::size_t e = 0; e < L.edges.size(); e++)
            if (L.edges[e].dist == 0 && L.edges[e].to == static_cast<int>(o)) t[o] = std::max(t[o], t[static_cast<std::size_t>(L.edges[e].from)] + L.edges[e].delay);
        len = std::max(len, t[o] + L.nodes[o].lat);
    }
    return std::max(len, 6);
}

// One try at an initiation interval: each instruction in program order at the first cycle in the window its
// scheduled neighbours leave it whose modulo slot has room - the branch and the count's step reserved first.
bool scheduleAt(Loop &L, int II, std::vector<int> &t, const std::set<std::string> &renameable, std::map<std::string, int> &minQ, std::vector<int> &push) {
    const std::size_t n = L.nodes.size();
    t.assign(n, -1);
    minQ.clear();
    std::uint64_t renameMask = 0;
    for (std::set<std::string>::const_iterator it = renameable.begin(); it != renameable.end(); ++it) renameMask |= bitOf(*it);
    std::vector<Packet> mrt(static_cast<std::size_t>(II));
    std::vector<Node> extra;
    extra.reserve(2 + n * 5);
    std::set<std::string> lab;
    lab.insert(L.label + "$pipe");
    extra.push_back(makeNode(rebuilt("B", std::vector<std::string>(1, L.label + "$pipe"), L.C), lab));
    extra.push_back(makeNode(make("SUB", L.C, "1", L.C), std::set<std::string>()));
    const int sb = ((6 % II) == 0 ? 0 : II - 6 % II);
    for (int k = 0; k < 2; k++) { if (!fits(mrt[static_cast<std::size_t>(sb)], extra[static_cast<std::size_t>(k)])) return false; add(mrt[static_cast<std::size_t>(sb)], extra[static_cast<std::size_t>(k)]); }
    for (std::size_t o = 0; o < n; o++) {
        const Node &x = L.nodes[o];
        if (x.late + 1 > II) return false;
        int lo = push[o], hi = 1 << 30, hiBy = -1;
        for (std::size_t e = 0; e < L.edges.size(); e++) {
            const Edge &d = L.edges[e];
            if (d.to == static_cast<int>(o) && d.from == static_cast<int>(o)) { if (d.delay > d.dist * II) return false; continue; }
            if (d.to == static_cast<int>(o) && t[static_cast<std::size_t>(d.from)] >= 0) lo = std::max(lo, t[static_cast<std::size_t>(d.from)] + d.delay - d.dist * II);
            if (d.from == static_cast<int>(o) && t[static_cast<std::size_t>(d.to)] >= 0) {
                if (tracing() > 2 && t[static_cast<std::size_t>(d.to)] - d.delay + d.dist * II < hi) std::fprintf(stderr, "    hi by%s (%s delay %d dist %d)\n", L.nodes[static_cast<std::size_t>(d.to)].line.raw.c_str(), d.why, d.delay, d.dist);
                if (t[static_cast<std::size_t>(d.to)] - d.delay + d.dist * II < hi) { hi = t[static_cast<std::size_t>(d.to)] - d.delay + d.dist * II; hiBy = d.dist ? d.to : -1; }
            }
        }
        bool placed = false;
        // A renamed register may be written twice in one slot: the two are different iterations' copies, so q says so.
        extra.push_back(x);
        Node &xv = extra.back();
        xv.writes &= ~renameMask;
        for (int c = lo; c <= hi && c < lo + II; c++) {
            const std::size_t s = static_cast<std::size_t>(c % II);
            if (!fits(mrt[s], xv)) continue;
            bool held = true;
            for (int k = 1; k <= x.late && held; k++) {
                Node ph; ph.units = x.units; ph.side = x.side;
                held = fits(mrt[static_cast<std::size_t>((c + k) % II)], ph);
            }
            if (!held) continue;
            t[o] = c;
            for (std::size_t m = 0; m < mrt[s].members.size(); m++) {
                const std::uint64_t both = mrt[s].members[m]->writes & x.writes & renameMask;
                if (!both) continue;
                for (std::size_t p = 0; p < o; p++)
                    if (t[p] >= 0 && t[p] % II == c % II && (L.nodes[p].writes & both))
                        for (int r = 0; r < 64; r++) if (both & (1ull << r)) { std::string name = std::string(r < 32 ? "A" : "B") + std::to_string(r % 32); minQ[name] = std::max(minQ[name], (c - t[p]) / II + 1); }
            }
            add(mrt[s], xv);
            for (int k = 1; k <= x.late; k++) {
                Node ph; ph.units = x.units; ph.side = x.side;
                extra.push_back(ph);
                add(mrt[static_cast<std::size_t>((c + k) % II)], extra.back());
            }
            placed = true;
            break;
        }
        if (!placed) {
            if (tracing() > 1) std::fprintf(stderr, "  II=%d: no slot for%s in [%d,%d]\n", II, x.line.raw.c_str(), lo, hi);
            if (hi < lo && hiBy >= 0) push[static_cast<std::size_t>(hiBy)] += lo - hi;
            return false;
        }
    }
    return true;
}

// A register's rename unit: itself, or the even:odd pair it is half of.
struct Unit { std::vector<std::string> regs; int q = 1; std::vector<std::vector<std::string> > copies; bool carried = false, exits = false; };

int pow2At(int x) { int p = 1; while (p < x) p *= 2; return p; }
long modq(long a, int q) { return ((a % q) + q) % q; }

std::string spelled(const Loop &L, const std::vector<Unit> &units, const std::map<std::string, std::size_t> &unitOf,
                    const std::vector<int> &firstWrite, std::size_t o, long j) {
    const Line &l = L.nodes[o].line;
    std::vector<std::string> ops = l.ops;
    std::string pred = l.pred;
    bool hasDest = !ops.empty() && !isStore(l.mnem) && l.mnem != "B" && ops.back()[0] != '*';
    std::size_t k0 = 0;
    for (std::map<std::string, std::size_t>::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k0++) {
        const Unit &u = units[it->second];
        if (u.q < 2) continue;
        const std::string &r = it->first;
        std::size_t half = 0;
        while (u.regs[half] != r) half++;
        const long readJ = j + (firstWrite[k0] >= static_cast<int>(o) ? -1 : 0);
        const std::string rd = u.copies[half][static_cast<std::size_t>(modq(readJ, u.q))], wr = u.copies[half][static_cast<std::size_t>(modq(j, u.q))];
        for (std::size_t k = 0; k < ops.size(); k++) {
            bool dest = hasDest && k + 1 == ops.size();
            if (dest && (l.mnem == "MVKH" || l.mnem == "ADDK")) { ops[k] = renamed(ops[k], r, wr); continue; }
            ops[k] = renamed(ops[k], r, dest ? wr : rd);
        }
        if (!pred.empty()) pred = renamed(pred, r, rd);
    }
    return rebuilt(l.mnem, ops, pred).raw;
}

Line verbatim(const std::string &raw) { Line l; l.raw = raw; l.verbatim = true; return l; }

void emitCycles(std::vector<Line> &out, const std::vector<std::vector<std::string> > &cycles) {
    int nops = 0;
    for (std::size_t c = 0; c < cycles.size(); c++) {
        if (cycles[c].empty()) { nops++; continue; }
        while (nops > 0) { out.push_back(verbatim("\tNOP\t" + std::to_string(std::min(nops, 9)))); nops -= std::min(nops, 9); }
        for (std::size_t k = 0; k < cycles[c].size(); k++) out.push_back(verbatim((k ? "||" : "") + cycles[c][k]));
    }
    while (nops > 0) { out.push_back(verbatim("\tNOP\t" + std::to_string(std::min(nops, 9)))); nops -= std::min(nops, 9); }
}

// A register web: one value, from its write to its last read; linked to its partner's where it is half a pair.
struct Web { std::string reg, name; int link = -1; bool keep = false; };

// Each write of a temporary starts a web of its own, and every web but the first and the one a later iteration or
// the exit reads takes a fresh name from the pool - so a name the backend recycles through an iteration becomes
// several short-lived ones, each renamed per iteration on its own account.
void renameWebs(Loop &L, std::vector<std::string> &pool) {
    std::vector<Web> webs;
    std::map<std::string, int> cur;
    std::set<std::string> fixed;                            // registers no web of which may be renamed
    std::vector<std::vector<std::pair<int, int> > > opWebs(L.body.size());   // (web, isWrite) per op
    for (std::size_t o = 0; o < L.body.size(); o++) {
        const Line &l = L.body[o];
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        bool rmw = l.mnem == "MVKH" || l.mnem == "ADDK" || !l.pred.empty();
        for (std::size_t r = 0; r < reads.size(); r++) {
            if (!cur.count(reads[r])) { Web w; w.reg = reads[r]; w.keep = true; cur[reads[r]] = static_cast<int>(webs.size()); webs.push_back(w); }
            opWebs[o].push_back(std::make_pair(cur[reads[r]], 0));
        }
        // A pair operand links its halves' current webs - the sources before the writes are seen, the
        // destination after; a half linked twice over keeps its names.
        bool hasDest = !l.ops.empty() && !isStore(l.mnem) && l.mnem != "B" && l.ops.back()[0] != '*';
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) for (std::size_t w = 0; w < writes.size(); w++) {
                if (!rmw || !cur.count(writes[w])) { Web nw; nw.reg = writes[w]; cur[writes[w]] = static_cast<int>(webs.size()); webs.push_back(nw); }
                opWebs[o].push_back(std::make_pair(cur[writes[w]], 1));
            }
            for (std::size_t k = 0; k < l.ops.size(); k++) {
                const bool dest = hasDest && k + 1 == l.ops.size();
                if (dest != (pass == 1)) continue;
                std::vector<std::string> regs;
                registersIn(l.ops[k], regs);
                if (l.ops[k].find(':') == std::string::npos || regs.size() != 2 || !cur.count(regs[0]) || !cur.count(regs[1])) continue;
                Web &hi = webs[static_cast<std::size_t>(cur[regs[0]])], &lo = webs[static_cast<std::size_t>(cur[regs[1]])];
                if ((hi.link >= 0 && hi.link != cur[regs[1]]) || (lo.link >= 0 && lo.link != cur[regs[0]])) { fixed.insert(regs[0]); fixed.insert(regs[1]); }
                hi.link = cur[regs[1]]; lo.link = cur[regs[0]];
            }
        }
    }
    for (std::map<std::string, int>::const_iterator it = cur.begin(); it != cur.end(); ++it) {
        const std::string &r = it->first;
        bool carried = false;
        for (std::size_t w = 0; w < webs.size(); w++) if (webs[w].reg == r && webs[w].keep) carried = true;
        if (carried || (L.liveExit & bitOf(r)) || r == L.iv || r == L.bound || r == L.C || r == L.P) webs[static_cast<std::size_t>(it->second)].keep = true;
    }
    for (std::size_t w = 0; w < webs.size(); w++) {
        Web &x = webs[w];
        if (x.name.empty()) x.name = x.reg;
        if (x.keep || fixed.count(x.reg) || x.reg == "A15" || x.reg == "B15" || x.reg == "B3" || x.reg == "B14") continue;
        if (x.link >= 0 && (webs[static_cast<std::size_t>(x.link)].keep || fixed.count(webs[static_cast<std::size_t>(x.link)].reg))) continue;
        if (x.name != x.reg) continue;                      // named with its partner already
        for (std::size_t f = 0; f < pool.size(); f++) {
            if (sideOf(pool[f]) != sideOf(x.reg)) continue;
            if (x.link < 0) { x.name = pool[f]; pool.erase(pool.begin() + static_cast<long>(f)); break; }
            int num = std::atoi(pool[f].c_str() + 1);
            if (num % 2) continue;
            std::string hi = std::string(1, pool[f][0]) + std::to_string(num + 1);
            std::vector<std::string>::iterator ih = std::find(pool.begin(), pool.end(), hi);
            if (ih == pool.end()) continue;
            Web &y = webs[static_cast<std::size_t>(x.link)];
            const bool xLow = std::atoi(x.reg.c_str() + 1) % 2 == 0;
            x.name = xLow ? pool[f] : hi; y.name = xLow ? hi : pool[f];
            pool.erase(ih); pool.erase(pool.begin() + static_cast<long>(f));
            break;
        }
    }
    for (std::size_t o = 0; o < L.body.size(); o++) {
        Line &l = L.body[o];
        std::vector<std::string> ops = l.ops;
        std::string pred = l.pred;
        bool hasDest = !ops.empty() && !isStore(l.mnem) && l.mnem != "B" && ops.back()[0] != '*';
        for (std::size_t k = 0; k < opWebs[o].size(); k++) {
            const Web &w = webs[static_cast<std::size_t>(opWebs[o][k].first)];
            if (w.name == w.reg) continue;
            const bool isWrite = opWebs[o][k].second == 1;
            for (std::size_t p = 0; p < ops.size(); p++) {
                bool dest = hasDest && p + 1 == ops.size();
                if (dest == isWrite || (dest && (l.mnem == "MVKH" || l.mnem == "ADDK"))) ops[p] = renamed(ops[p], w.reg, w.name);
            }
            if (!isWrite) pred = renamed(pred, w.reg, w.name);
        }
        l = rebuilt(l.mnem, ops, pred);
    }
    L.used.clear(); L.written.clear(); L.predicated.clear();
    for (std::size_t o = 0; o < L.body.size(); o++) {
        std::vector<std::string> reads, writes;
        readsAndWrites(L.body[o], reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) L.used.insert(reads[r]);
        for (std::size_t w = 0; w < writes.size(); w++) { L.used.insert(writes[w]); L.written.insert(writes[w]); if (!L.body[o].pred.empty()) L.predicated.insert(writes[w]); }
    }
    L.used.insert(L.P);
    std::set<std::string> seen;
    for (std::size_t o = 0; o < L.body.size(); o++) {
        std::vector<std::string> reads, writes;
        readsAndWrites(L.body[o], reads, writes);
        for (std::size_t w = 0; w < writes.size(); w++) {
            if (!seen.count(writes[w]) && (L.body[o].mnem == "MVKH" || L.body[o].mnem == "ADDK")) L.rmwFirst.insert(writes[w]);
            seen.insert(writes[w]);
        }
    }
}

// The whole rewrite of one recognised loop; false, with the reason, where it is not worth it or cannot be done.
bool pipeline(const std::vector<Line> &v, Loop &L, std::vector<Line> &out, std::string &why) {
    // The pool of names the loop may take: caller-saved, unused in it, dead at its exit.
    std::vector<std::string> pool;
    for (int side = 0; side < 2; side++)
        for (int k = 3; k < 32; k++) {
            if ((k >= 10 && k <= 15) || (side == 1 && k == 3)) continue;
            std::string r = std::string(side ? "B" : "A") + std::to_string(k);
            if (!L.used.count(r) && !(L.liveExit & bitOf(r)) && r != L.C && r != L.P) pool.push_back(r);
        }
    std::string X;
    for (std::size_t k = 0; k < pool.size() && X.empty(); k++) if (sideOf(pool[k]) == sideOf(L.C)) X = pool[k];
    if (X.empty()) { why = "no free register for the count"; return false; }
    pool.erase(std::find(pool.begin(), pool.end(), X));
    renameWebs(L, pool);
    std::set<std::string> labels;
    labels.insert(L.label + "$pipe");
    for (std::size_t o = 0; o < L.body.size(); o++) L.nodes.push_back(makeNode(L.body[o], labels));
    // The rename units, and which of them may take a name per iteration.
    std::vector<Unit> units;
    std::map<std::string, std::size_t> unitOf;
    for (std::size_t o = 0; o < L.body.size(); o++)
        for (std::size_t k = 0; k < L.body[o].ops.size(); k++) {
            std::vector<std::string> regs;
            registersIn(L.body[o].ops[k], regs);
            if (L.body[o].ops[k].find(':') == std::string::npos || regs.size() != 2) continue;
            if (!unitOf.count(regs[0]) && !unitOf.count(regs[1])) { Unit u; u.regs.push_back(regs[1]); u.regs.push_back(regs[0]); unitOf[regs[0]] = unitOf[regs[1]] = units.size(); units.push_back(u); }
        }
    for (std::set<std::string>::const_iterator it = L.used.begin(); it != L.used.end(); ++it)
        if (!unitOf.count(*it)) { Unit u; u.regs.push_back(*it); unitOf[*it] = units.size(); units.push_back(u); }
    std::vector<int> firstWrite(unitOf.size(), 1 << 30);
    {
        std::size_t k = 0;
        for (std::map<std::string, std::size_t>::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k++)
            for (std::size_t o = 0; o < L.nodes.size(); o++) if (L.nodes[o].writes & bitOf(it->first)) { firstWrite[k] = static_cast<int>(o); break; }
    }
    std::set<std::string> renameable;
    for (std::size_t u = 0; u < units.size(); u++) {
        bool ok = true;                                     // every half written: a pair's names move together
        for (std::size_t h = 0; h < units[u].regs.size(); h++) {
            const std::string &r = units[u].regs[h];
            if (!L.written.count(r)) ok = false;
            if (L.predicated.count(r) || L.rmwFirst.count(r) || r == L.C || r == "A15" || r == "B15" || r == "B3" || r == "B14") { ok = false; break; }
        }
        if (ok) for (std::size_t h = 0; h < units[u].regs.size(); h++) renameable.insert(units[u].regs[h]);
    }
    if (tracing() > 3) for (std::size_t o = 0; o < L.body.size(); o++) std::fprintf(stderr, "  body%s\n", L.body[o].raw.c_str());
    if (tracing() > 2) { std::fprintf(stderr, "  renameable:"); for (std::set<std::string>::const_iterator it = renameable.begin(); it != renameable.end(); ++it) std::fprintf(stderr, " %s", it->c_str()); std::fprintf(stderr, "\n"); }
    std::vector<int> t;
    std::map<std::string, int> minQ;
    int II = 1, S = 0, u = 1, seq = 0;
    for (int attempt = 0; attempt < 8; attempt++) {
        buildEdges(L, renameable);
        seq = criticalPath(L);
        int lateMax = 0;
        for (std::size_t o = 0; o < L.nodes.size(); o++) lateMax = std::max(lateMax, L.nodes[o].late);
        // Each II in turn: a schedule, then the copies its spans need; an II whose copies the pool cannot
        // give is passed over for the next, and only past the bound are the units without them left unrenamed.
        bool any = false, done = false;
        std::vector<std::string> failed;
        for (II = lateMax + 1; II <= 64 && 5 * II <= 4 * seq; II++) {
            bool ok = false;
            std::vector<int> push(L.nodes.size(), 0), was;
            for (int retry = 0; retry < 8 && !ok; retry++) { was = push; ok = scheduleAt(L, II, t, renameable, minQ, push); if (push == was) break; }
            if (!ok) continue;
            any = true;
            failed.clear();
            int len = 0;
            for (std::size_t o = 0; o < t.size(); o++) len = std::max(len, t[o] + 1);
            S = (len + II - 1) / II;
            // Each unit's span from its first write's landing to its last use, the reads of the old value a turn on.
            int qMax = 1;
            std::size_t k = 0;
            for (std::size_t x = 0; x < units.size(); x++) { units[x].q = 1; units[x].carried = units[x].exits = false; }
            for (std::map<std::string, std::size_t>::const_iterator it = unitOf.begin(); it != unitOf.end(); ++it, k++) {
                Unit &un = units[it->second];
                const std::uint64_t bit = bitOf(it->first);
                if (!renameable.count(it->first) || !L.written.count(it->first)) continue;
                int start = 1 << 30, end = 0;
                for (std::size_t o = 0; o < L.nodes.size(); o++) {
                    if (L.nodes[o].writes & bit) { start = std::min(start, t[o] + L.nodes[o].lat); end = std::max(end, t[o] + L.nodes[o].lat); }
                    if (L.nodes[o].reads & bit) end = std::max(end, t[o] + L.nodes[o].late + (firstWrite[k] >= static_cast<int>(o) ? II : 0));
                }
                int q = std::max((end - start) / II + 1, minQ.count(it->first) ? minQ[it->first] : 1);
                un.q = std::max(un.q, pow2At(q));
                if (firstWrite[k] < (1 << 30)) for (std::size_t o = 0; o <= static_cast<std::size_t>(firstWrite[k]); o++) if (L.nodes[o].reads & bit) un.carried = true;
                if ((L.liveExit & bit) || it->first == L.iv || it->first == L.bound) un.exits = true;
            }
            for (std::size_t x = 0; x < units.size(); x++) qMax = std::max(qMax, units[x].q);
            u = pow2At(std::max((6 + II - 1) / II, qMax));
            // The copies: the register itself, then q-1 more of its side - a pair's two halves aligned.
            std::vector<std::string> free = pool;
            for (std::size_t x = 0; x < units.size(); x++) {
                Unit &un = units[x];
                un.copies.assign(un.regs.size(), std::vector<std::string>());
                for (std::size_t h = 0; h < un.regs.size(); h++) un.copies[h].push_back(un.regs[h]);
                for (int c = 1; c < un.q; c++) {
                    bool got = false;
                    for (std::size_t f = 0; f < free.size() && !got; f++) {
                        if (sideOf(free[f]) != sideOf(un.regs[0])) continue;
                        if (un.regs.size() == 1) { un.copies[0].push_back(free[f]); free.erase(free.begin() + static_cast<long>(f)); got = true; break; }
                        int num = std::atoi(free[f].c_str() + 1);
                        if (num % 2) continue;
                        std::string hi = std::string(1, free[f][0]) + std::to_string(num + 1);
                        std::vector<std::string>::iterator ih = std::find(free.begin(), free.end(), hi);
                        if (ih == free.end()) continue;
                        un.copies[0].push_back(free[f]); un.copies[1].push_back(hi);
                        free.erase(ih); free.erase(free.begin() + static_cast<long>(f)); got = true;
                    }
                    if (!got) { failed.push_back(un.regs[0]); break; }
                }
            }
            if (failed.empty()) { done = true; break; }
        }
        if (!any) { why = "no schedule under four fifths of the " + std::to_string(seq) + "-cycle iteration"; return false; }
        if (done) break;
        for (std::size_t f = 0; f < failed.size(); f++) {
            const Unit &un = units[unitOf[failed[f]]];
            if (tracing() > 1) std::fprintf(stderr, "  II=%d: no %d copies of %s\n", II, un.q, failed[f].c_str());
            for (std::size_t h = 0; h < un.regs.size(); h++) renameable.erase(un.regs[h]);
        }
        if (attempt == 7) { why = "no registers for the renaming"; return false; }
    }
    if (tracing()) std::fprintf(stderr, "pipe %s: II=%d S=%d u=%d seq=%d ops=%zu\n", L.label.c_str(), II, S, u, seq, L.nodes.size());
    // The guard: X = (n - i [+1] - (S-1)) >> log2 u, the unrolled rounds; none, and the loop is left as written.
    const bool crossBoth = !L.boundConst && sideOf(L.bound) != sideOf(X) && sideOf(L.iv) != sideOf(X);
    if (L.boundConst) {
        if (L.boundK >= -32768 && L.boundK <= 32767) out.push_back(make("MVK", std::to_string(L.boundK), X));
        else { out.push_back(make("MVKL", std::to_string(L.boundK), X)); out.push_back(make("MVKH", std::to_string(L.boundK), X)); }
        out.push_back(make("SUB", X, L.iv, X));
    } else if (crossBoth) { out.push_back(make("MV", L.bound, X)); out.push_back(make("SUB", X, L.iv, X)); }
    else out.push_back(make("SUB", L.bound, L.iv, X));
    if (L.plusOne) out.push_back(make("ADD", "1", X, X));
    if (S > 1) { if (S - 1 <= 31) out.push_back(make("SUB", X, std::to_string(S - 1), X)); else out.push_back(make("ADDK", std::to_string(1 - S), X)); }
    int log2u = 0;
    while ((1 << log2u) < u) log2u++;
    if (log2u) out.push_back(make("SHR", X, std::to_string(log2u), X));
    out.push_back(make("CMPGT", "1", X, L.P));
    out.push_back(rebuilt("B", std::vector<std::string>(1, L.label), L.P));
    out.push_back(make("SUB", X, "1", L.C));
    for (std::size_t x = 0; x < units.size(); x++)
        if (units[x].q > 1 && units[x].carried) for (std::size_t h = 0; h < units[x].regs.size(); h++) out.push_back(make("MV", units[x].regs[h], units[x].copies[h][static_cast<std::size_t>(units[x].q - 1)]));
    // The prologue, the kernel of u rounds with its branch, and the epilogue - every op of every iteration once.
    const std::size_t n = L.nodes.size();
    std::vector<std::vector<std::string> > cyc;
    for (int a = 0; a < (S - 1) * II; a++) {
        std::vector<std::string> p;
        for (std::size_t o = 0; o < n; o++) if (t[o] <= a && (a - t[o]) % II == 0) p.push_back(spelled(L, units, unitOf, firstWrite, o, (a - t[o]) / II));
        cyc.push_back(p);
    }
    emitCycles(out, cyc);
    cyc.clear();
    out.push_back(verbatim(L.label + "$pipe:"));
    for (int c = 0; c < u; c++)
        for (int s = 0; s < II; s++) {
            std::vector<std::string> p;
            for (std::size_t o = 0; o < n; o++) if (t[o] % II == s) p.push_back(spelled(L, units, unitOf, firstWrite, o, S - 1 + c - t[o] / II));
            if (c * II + s == u * II - 6) { p.push_back(rebuilt("B", std::vector<std::string>(1, L.label + "$pipe"), L.C).raw); p.push_back(make("SUB", L.C, "1", L.C).raw); }
            cyc.push_back(p);
        }
    emitCycles(out, cyc);
    cyc.clear();
    int epi = (S - 1) * II;
    for (std::size_t o = 0; o < n; o++) epi = std::max(epi, t[o] + L.nodes[o].lat - II);
    for (int e = 0; e < epi; e++) {
        std::vector<std::string> p;
        for (std::size_t o = 0; o < n; o++) if (t[o] - e >= II && (t[o] - e) % II == 0) p.push_back(spelled(L, units, unitOf, firstWrite, o, S - 1 - (t[o] - e) / II));
        cyc.push_back(p);
    }
    emitCycles(out, cyc);
    // The last pipelined iteration is u*q'+S-2, so its names are fixed: copied back, then the test the loop's own way.
    for (std::size_t x = 0; x < units.size(); x++)
        if (units[x].q > 1 && units[x].exits && modq(S - 2, units[x].q) != 0) for (std::size_t h = 0; h < units[x].regs.size(); h++) out.push_back(make("MV", units[x].copies[h][static_cast<std::size_t>(modq(S - 2, units[x].q))], units[x].regs[h]));
    out.push_back(v[L.cmp]);
    out.push_back(rebuilt("B", std::vector<std::string>(1, L.exitName), L.negated ? L.P : "!" + L.P));
    return true;
}

}   // namespace

// Every candidate loop of the text, innermost first as the text is walked: rewritten in place where the pipelining pays.
void pipelineLoops(std::vector<Line> &v) {
    computeLiveness(v);
    std::set<std::string> labels, named;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) labels.insert(labelName(v[i]));
        for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    }
    std::vector<Line> out;
    std::size_t copied = 0;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (!v[i].instr || v[i].mnem != "B" || v[i].pred.empty() || isCall(v[i], labels)) continue;
        Loop L;
        std::string why;
        if (!recognise(v, i, labels, named, L, why)) { if (tracing()) std::fprintf(stderr, "pipe %s: %s\n", v[i].ops[0].c_str(), why.c_str()); continue; }
        if (L.head < copied) continue;
        std::vector<Line> code;
        if (!pipeline(v, L, code, why)) { if (tracing()) std::fprintf(stderr, "pipe %s: %s\n", L.label.c_str(), why.c_str()); continue; }
        for (std::size_t k = copied; k < L.head; k++) out.push_back(v[k]);
        for (std::size_t k = 0; k < code.size(); k++) out.push_back(code[k]);
        copied = L.head;
    }
    for (std::size_t k = copied; k < v.size(); k++) out.push_back(v[k]);
    v.swap(out);
}

}   // namespace c6x
