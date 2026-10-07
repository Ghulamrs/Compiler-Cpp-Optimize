// Software pipelining of the innermost counted loops of one function's C6000 text at -O2: a loop that is one block
// ending in `[P] B head`, counted by `ADD 1, i, i` against a bound, is modulo-scheduled at the least II its dependences
// and units allow, renamed per iteration where a value outlives II, and emitted as prologue, kernel and epilogue.

#include "C6xModel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
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
    // A pointer the accesses alone step: on entry to a turn it is the preheader's value plus its stride per turn gone.
    std::map<std::string, long> stride, stepWrites, allWrites;
    for (std::size_t o = 0; o < body.size(); o++) {
        std::vector<std::string> reads, writes;
        readsAndWrites(body[o], reads, writes);
        for (std::size_t w = 0; w < writes.size(); w++) allWrites[writes[w]]++;
        long by;
        const std::string r = steppedRegister(body[o], by);
        if (!r.empty()) { stride[r] += by; stepWrites[r]++; }
    }
    const Form turns = combine(formOf("iv"), formOf("reg:" + iv), -1);
    std::vector<Access> out(body.size());
    for (std::size_t o = 0; o < body.size(); o++) {
        const Line &l = body[o];
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) {
            if (val.count(reads[r]) || lost.count(reads[r])) continue;
            const std::string &x = reads[r];
            if (x == iv) val[x] = formOf("iv");
            else if (stepWrites.count(x) && stepWrites[x] == allWrites[x] && before.count(x) && x != iv) val[x] = combine(before[x], scaled(turns, stride[x]), 1);
            else val[x] = written.count(x) ? Form() : before.count(x) ? before[x] : formOf(x == "A15" ? "A15" : "reg:" + x);
        }
        long by = 0;
        const std::string stepReg = steppedRegister(l, by);
        Form stepFrom; bool stepKnown = !stepReg.empty() && val.count(stepReg) && !val[stepReg].empty();
        if (stepKnown) stepFrom = val[stepReg];
        if (isLoad(l.mnem) || isStore(l.mnem)) {
            const std::string &m = isStore(l.mnem) ? l.ops[1] : l.ops[0];
            std::string base; long off = 0; std::string index; long sign = 1;
            if (!stepReg.empty()) { base = stepReg; if (m.compare(0, 3, "*++") == 0 || m.compare(0, 3, "*--") == 0) off = by, sign = 1; }
            else if (m.size() > 1 && m[0] == '*' && (m[1] == '+' || m[1] == '-') && m.find('(') != std::string::npos) {
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
        if (stepKnown) { val[stepReg] = combine(stepFrom, formOf("", by), 1); lost.erase(stepReg); }
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
    // A counter stepped by more than one: the test stays in the body and the kernel's branch reads it against a bound
    // moved back by the turns in flight, so no turn the loop would not have run is begun. Low word first throughout.
    bool stepped = false, wide = false;
    std::vector<std::string> ivs, bounds, steps;
    std::vector<std::size_t> testBody;          // the body's compares that read a bound
    // A counter's word, a bound or a step the kernel and the remainder both read under its own name.
    bool pinned(const std::string &r) const {
        return std::find(ivs.begin(), ivs.end(), r) != ivs.end() || std::find(bounds.begin(), bounds.end(), r) != bounds.end() ||
               std::find(steps.begin(), steps.end(), r) != steps.end();
    }
};

const char *const kCond[] = { "A0", "A1", "A2", "B0", "B1", "B2" };

// ----- the body read as algebra: what each register holds at its end, as a term over the values it came in with -----

// A term is a string: `s:R` the value R held at entry, `k:N` a constant, `sum{a,b,..}` a sorted flattened sum, `lt(a,b)`,
// `ltu(a,b)`, `eq{a,b}`, `and{a,b}`, `or{a,b}` the compares and logic (the braces sorted), and `op:N` anything else.
std::string joinSorted(std::vector<std::string> ts) {
    std::sort(ts.begin(), ts.end());
    std::string s;
    for (std::size_t i = 0; i < ts.size(); i++) s += (i ? "," : "") + ts[i];
    return s;
}
// The terms between the brackets of one, split at the commas of its own level.
std::vector<std::string> splitTop(const std::string &inner) {
    std::vector<std::string> parts;
    int depth = 0; std::size_t from = 0;
    for (std::size_t c = 0; c <= inner.size(); c++) {
        if (c == inner.size() || (inner[c] == ',' && depth == 0)) { parts.push_back(inner.substr(from, c - from)); from = c + 1; }
        else if (inner[c] == '(' || inner[c] == '{') depth++;
        else if (inner[c] == ')' || inner[c] == '}') depth--;
    }
    return parts;
}
std::string sumOf(const std::string &a, const std::string &b) {
    std::vector<std::string> ts;
    const std::string *parts[2] = { &a, &b };
    for (int i = 0; i < 2; i++) {
        const std::string &p = *parts[i];
        if (p.compare(0, 4, "sum{") == 0) { std::vector<std::string> in = splitTop(p.substr(4, p.size() - 5)); ts.insert(ts.end(), in.begin(), in.end()); }
        else ts.push_back(p);
    }
    return "sum{" + joinSorted(ts) + "}";
}

// The terms of every register after the body, and the term P is computed from, by one pass in program order.
std::map<std::string, std::string> evaluateBody(const std::vector<Line> &v, const Loop &L, std::vector<std::string> &lineTerms) {
    std::map<std::string, std::string> env;
    auto term = [&](const std::string &op) -> std::string {
        if (isNumber(op)) return "k:" + op;
        if (!env.count(op)) env[op] = "s:" + op;
        return env[op];
    };
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const Line &l = v[L.bodyAt[k]];
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) term(reads[r]);
        std::string t;
        const bool plain = l.pred.empty() && l.ops.size() == 3 && writes.size() == 1 && l.ops[2] == writes[0];
        if (l.mnem == "MV" && l.pred.empty() && l.ops.size() == 2 && writes.size() == 1) t = term(l.ops[0]);
        else if (plain && l.mnem == "ADD") t = sumOf(term(l.ops[0]), term(l.ops[1]));
        else if (plain && l.mnem == "CMPLT") t = "lt(" + term(l.ops[0]) + "," + term(l.ops[1]) + ")";
        else if (plain && l.mnem == "CMPGT") t = "lt(" + term(l.ops[1]) + "," + term(l.ops[0]) + ")";
        else if (plain && l.mnem == "CMPLTU") t = "ltu(" + term(l.ops[0]) + "," + term(l.ops[1]) + ")";
        else if (plain && l.mnem == "CMPGTU") t = "ltu(" + term(l.ops[1]) + "," + term(l.ops[0]) + ")";
        else if (plain && (l.mnem == "CMPEQ" || l.mnem == "AND" || l.mnem == "OR")) {
            std::vector<std::string> ts; ts.push_back(term(l.ops[0])); ts.push_back(term(l.ops[1]));
            t = (l.mnem == "CMPEQ" ? "eq{" : l.mnem == "AND" ? "and{" : "or{") + joinSorted(ts) + "}";
        }
        for (std::size_t w = 0; w < writes.size(); w++) env[writes[w]] = t.empty() || writes.size() > 1 ? "op:" + std::to_string(k) + ":" + writes[w] : t;
        lineTerms.push_back(t.empty() || writes.size() != 1 ? "" : env[writes[0]]);
    }
    return env;
}

// **A counter stepped by more than one, or sixty-four bits wide**: the body, read as algebra, must leave registers lo (and
// hi) as `lo + s` (and `hi + shi + carry`) for s the loop never writes, and compute P as `lo' < n` or `n < lo'` - the two-word
// signed form `hi'<nhi | (hi'==nhi & lo' <u nlo)` for a wide one - so the loop runs while iv < n or iv <= n by its own test.
bool recogniseStepped(const std::vector<Line> &v, Loop &L, std::string &why, const std::string &fallback) {
    why = fallback;
    auto no = [&](const char *r) { if (tracing() > 1) why += std::string(" [stepped: ") + r + "]"; return false; };
    L.liveExit = v[L.back + 1].liveIn;
    if (L.liveExit & bitOf(L.P)) return no("the predicate lives past the loop");
    std::map<std::string, int> writers, readers;
    std::map<std::string, std::size_t> writerAt;
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        std::vector<std::string> reads, writes;
        readsAndWrites(v[L.bodyAt[k]], reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) readers[reads[r]]++;
        for (std::size_t w = 0; w < writes.size(); w++) { writers[writes[w]]++; writerAt[writes[w]] = k; }
    }
    if (writers[L.P] != 1) return no("the predicate is written more than once");
    std::vector<std::string> lineTerms;
    std::map<std::string, std::string> env = evaluateBody(v, L, lineTerms);
    // The steps: a register whose end term is itself plus one invariant (lo), and one that adds a carry of lo's sum (hi).
    auto invariant = [&](const std::string &t, std::string &reg) -> bool {
        if (t.compare(0, 2, "k:") == 0) { reg = t.substr(2); return true; }
        if (t.compare(0, 2, "s:") != 0) return false;
        reg = t.substr(2);
        return sideOf(reg) && !writers.count(reg);
    };
    std::string lo, slo, hi, shi;
    for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end() && lo.empty(); ++it) {
        const std::string &r = it->first, &t = it->second;
        if (!sideOf(r) || !writers.count(r) || t.compare(0, 4, "sum{") != 0) continue;
        const std::string a = "s:" + r;
        if (t == sumOf(a, "k:0") || env[L.P].find(t) == std::string::npos) continue;   // the counter the test reads
        std::vector<std::string> ts = splitTop(t.substr(4, t.size() - 5));
        if (ts.size() != 2) continue;
        std::string x = ts[0], y = ts[1], s;
        const std::string other = x == a ? y : y == a ? x : "";
        if (other.empty() || !invariant(other, s) || (s != r && s == "0")) continue;
        lo = r; slo = s;
    }
    if (lo.empty()) return no("no counter stepped by an invariant");
    const std::string loTerm = env[lo], carry = "ltu(" + loTerm + "," + (isNumber(slo) ? "k:" : "s:") + slo + ")";
    for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end() && hi.empty(); ++it) {
        const std::string &r = it->first, &t = it->second;
        if (!sideOf(r) || r == lo || !writers.count(r) || t.compare(0, 4, "sum{") != 0 || t.find(carry) == std::string::npos) continue;
        std::string rest = sumOf("s:" + r, carry), s;
        // t is sum{s:r, carry, step}: the step is what remains when those two are taken out.
        std::string cand;
        std::vector<std::string> parts = splitTop(t.substr(4, t.size() - 5));
        if (parts.size() != 3) continue;
        for (std::size_t p = 0; p < parts.size(); p++) if (parts[p] != "s:" + r && parts[p] != carry) cand = parts[p];
        if (cand.empty() || !invariant(cand, s) || sumOf(rest, cand) != t) continue;
        hi = r; shi = s;
    }
    L.wide = !hi.empty();
    if (L.wide && (isNumber(slo) != isNumber(shi))) return no("the two steps are not both registers or both constants");
    // The predicate: the stepped value against a bound the loop never writes, one or two words, signed.
    const std::string p = env[L.P], hiTerm = L.wide ? env[hi] : "";
    if (tracing() > 2) std::fprintf(stderr, "  stepped: lo %s + %s, hi %s + %s, %s = %s\n", lo.c_str(), slo.c_str(), hi.c_str(), shi.c_str(), L.P.c_str(), p.c_str());
    std::string nlo, nhi;
    bool less = false;                    // the loop runs while iv < n; else while iv <= n
    auto boundOf = [&](const std::string &t, std::string &reg) -> bool { return t.compare(0, 2, "s:") == 0 && invariant(t, reg) && reg != lo && reg != hi; };
    if (!L.wide) {
        if (p.compare(0, 3, "lt(") != 0) return no("the predicate is not a compare");
        std::vector<std::string> xy = splitTop(p.substr(3, p.size() - 4));
        if (xy.size() != 2) return no("the predicate is not a compare");
        const std::string x = xy[0], y = xy[1];
        if (x == loTerm && boundOf(y, nlo)) less = true;
        else if (y == loTerm && boundOf(x, nlo)) less = false;
        else return no("the compare is not of the stepped counter against an invariant");
    } else {
        // or{and{eq{hi',nhi},ltu(lo',nlo)},lt(hi',nhi)} for iv < n; the operands the other way round for n < iv.
        for (int dir = 0; dir < 2 && nlo.empty(); dir++) {
            for (std::map<std::string, std::string>::const_iterator a = env.begin(); a != env.end() && nlo.empty(); ++a) {
                std::string nl, nh;
                if (!boundOf("s:" + a->first, nl) || a->first == lo || a->first == hi) continue;
                for (std::map<std::string, std::string>::const_iterator b = env.begin(); b != env.end() && nlo.empty(); ++b) {
                    if (!boundOf("s:" + b->first, nh) || b->first == a->first) continue;
                    const std::string NL = "s:" + nl, NH = "s:" + nh;
                    std::vector<std::string> e; e.push_back(hiTerm); e.push_back(NH);
                    std::string ltu = dir == 0 ? "ltu(" + loTerm + "," + NL + ")" : "ltu(" + NL + "," + loTerm + ")";
                    std::string lt = dir == 0 ? "lt(" + hiTerm + "," + NH + ")" : "lt(" + NH + "," + hiTerm + ")";
                    std::vector<std::string> an; an.push_back("eq{" + joinSorted(e) + "}"); an.push_back(ltu);
                    std::vector<std::string> o; o.push_back("and{" + joinSorted(an) + "}"); o.push_back(lt);
                    if (p == "or{" + joinSorted(o) + "}") { nlo = nl; nhi = nh; less = dir == 0; }
                }
            }
        }
        if (nlo.empty()) return no("the two-word compare is not of the stepped counter against an invariant");
    }
    // `[P] B` with P = iv < n runs while iv < n; `[!P] B` with P = n < iv runs while iv <= n; the other two count down.
    if (less == L.negated) return no("the branch sense runs the loop while the test fails");
    L.plusOne = !less;
    // The lines that feed P, by the last writer before each read; the compares reading a bound must be among them,
    // must compare the stepped counter's term, and must be read by nothing else - they are the lines the kernel's
    // moved bound is substituted into.
    auto lastWriterBefore = [&](const std::string &x, std::size_t k) -> long {
        for (std::size_t j = k; j-- > 0;) {
            std::vector<std::string> reads, writes;
            readsAndWrites(v[L.bodyAt[j]], reads, writes);
            if (has(writes, x)) return static_cast<long>(j);
        }
        return -1;
    };
    std::set<std::size_t> feeding;
    std::vector<std::size_t> work(1, writerAt[L.P]);
    while (!work.empty()) {
        std::size_t k = work.back(); work.pop_back();
        if (!feeding.insert(k).second) continue;
        std::vector<std::string> reads, writes;
        readsAndWrites(v[L.bodyAt[k]], reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) { long w = lastWriterBefore(reads[r], k); if (w >= 0) work.push_back(static_cast<std::size_t>(w)); }
    }
    std::vector<std::size_t> tree;
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const Line &l = v[L.bodyAt[k]];
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        const bool rlo = has(reads, nlo), rhi = L.wide && has(reads, nhi);
        if (!rlo && !rhi) continue;
        if (!feeding.count(k) || l.ops.size() != 3 || !l.pred.empty() || l.mnem.compare(0, 3, "CMP") != 0 || writes.size() != 1) return no("a bound is read outside the test");
        if (l.ops[2] == nlo || l.ops[2] == nhi) return no("the test writes a bound");
        const std::string &lt = lineTerms[k], want = rlo ? loTerm : hiTerm, bound = "s:" + (rlo ? nlo : nhi);
        if (lt.empty() || lt.find(want) == std::string::npos || lt.find(bound) == std::string::npos) return no("the test reads the counter before its step");
        for (std::size_t k2 = k + 1; k2 < L.bodyAt.size(); k2++) {
            std::vector<std::string> r2, w2;
            readsAndWrites(v[L.bodyAt[k2]], r2, w2);
            if (has(r2, writes[0]) && !feeding.count(k2)) return no("a bound's compare is read outside the test");
            if (has(w2, writes[0])) break;
        }
        tree.push_back(k);
    }
    if (tree.empty()) return no("no compare reads the bound");
    if (isNumber(nlo) || (L.wide && isNumber(nhi))) return no("a constant bound");
    if (isNumber(slo) && L.wide) return no("a constant step on a two-word counter");
    if (isNumber(slo) && std::atol(slo.c_str()) <= 0) return no("a step that is not positive");
    L.stepped = true;
    L.iv = lo; L.bound = nlo; L.cmp = L.bodyAt[writerAt[L.P]];
    L.ivs.push_back(lo); L.bounds.push_back(nlo); L.steps.push_back(slo);
    if (L.wide) { L.ivs.push_back(hi); L.bounds.push_back(nhi); L.steps.push_back(shi); }
    L.used.clear(); L.written.clear(); L.predicated.clear();
    for (std::size_t k = 0; k < L.bodyAt.size(); k++) {
        const Line &l = v[L.bodyAt[k]];
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) L.used.insert(reads[r]);
        for (std::size_t w = 0; w < writes.size(); w++) { L.used.insert(writes[w]); L.written.insert(writes[w]); if (!l.pred.empty()) L.predicated.insert(writes[w]); }
        if (l.mnem == "MV" && l.ops.size() == 2 && l.ops[0] == l.ops[1]) continue;
        if (std::find(tree.begin(), tree.end(), k) != tree.end()) L.testBody.push_back(L.body.size());
        L.body.push_back(l);
    }
    why.clear();
    return true;
}

bool recognise(const std::vector<Line> &v, std::size_t back, const std::set<std::string> &labels, const std::set<std::string> &named, Loop &L, std::string &why) {
    const Line &b = v[back];
    if (b.pred.empty() || labels.count(b.ops[0]) == 0) { why = "no conditional branch to a label"; return false; }
    L.label = b.ops[0];
    if (L.label.size() > 5 && L.label.compare(L.label.size() - 5, 5, "$fill") == 0) { why = "a fill loop, memory-bound, kept as written"; return false; }
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
    if ((!lt && !gt) || c.ops.size() != 3 || !c.pred.empty() || c.ops[2] != L.P) return recogniseStepped(v, L, why, "the test is not a compare of two");
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
            if (!step || L.bodyAt[k] > L.cmp) return recogniseStepped(v, L, why, "the counter is not stepped by one before the test");
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
        // A writer's result lands at its latency - but an address it steps lands in E1, a cycle after issue.
        auto latOf = [&](int w) { return (n[static_cast<std::size_t>(w)].stepped & bitOf(it->first)) ? 1 : n[static_cast<std::size_t>(w)].lat; };
        int firstW = -1, lastW = -1;
        for (std::size_t k = 0; k < a.size(); k++) if (a[k].second) { if (firstW < 0) firstW = static_cast<int>(k); lastW = static_cast<int>(k); }
        if (firstW < 0) continue;
        for (std::size_t k = 0; k < a.size(); k++) {
            int prevW = -1, nextW = -1;
            for (int m = static_cast<int>(k) - 1; m >= 0; m--) if (a[static_cast<std::size_t>(m)].second) { prevW = m; break; }
            for (std::size_t m = k + 1; m < a.size(); m++) if (a[m].second) { nextW = static_cast<int>(m); break; }
            const int op = a[k].first;
            if (!a[k].second) {
                if (prevW >= 0 && a[static_cast<std::size_t>(prevW)].first != op) { Edge e = { a[static_cast<std::size_t>(prevW)].first, op, latOf(a[static_cast<std::size_t>(prevW)].first), 0, "flow" }; L.edges.push_back(e); }
                if (prevW < 0) { Edge e = { a[static_cast<std::size_t>(lastW)].first, op, latOf(a[static_cast<std::size_t>(lastW)].first), 1, "flow1" }; L.edges.push_back(e); }
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
        // The sequential loop's branch waits for its predicate - the body's own, or the compare of the stepped counter - and
        // the next turn starts six cycles after it, which the overlapped kernel never pays.
        const std::uint64_t gate = bitOf(L.stepped ? L.P : L.iv);
        if (L.nodes[o].writes & gate) len = std::max(len, t[o] + L.nodes[o].lat + (L.stepped ? 0 : 1) + 6);
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
    // The kernel's branch, on the count where there is one and on the loop's own test where the counter is stepped.
    extra.push_back(makeNode(rebuilt("B", std::vector<std::string>(1, L.label + "$pipe"), L.stepped ? (L.negated ? "!" + L.P : L.P) : L.C), lab));
    if (!L.stepped) extra.push_back(makeNode(make("ADD", "-1", L.C, L.C), std::set<std::string>()));
    const int sb = ((6 % II) == 0 ? 0 : II - 6 % II);
    for (std::size_t k = 0; k < extra.size(); k++) { if (!fits(mrt[static_cast<std::size_t>(sb)], extra[k])) return false; add(mrt[static_cast<std::size_t>(sb)], extra[k]); }
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
                Node ph; ph.units = x.units; ph.side = x.side; ph.cross = x.cross;
                held = fits(mrt[static_cast<std::size_t>((c + k) % II)], ph);
            }
            if (!held) continue;
            t[o] = c;
            add(mrt[s], xv);
            for (int k = 1; k <= x.late; k++) {
                Node ph; ph.units = x.units; ph.side = x.side; ph.cross = x.cross;
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
    // Two writes of one renamed register in one slot are different iterations' copies: q must tell them apart.
    for (std::size_t o = 0; o < n; o++)
        for (std::size_t p = 0; p < o; p++) {
            const std::uint64_t both = L.nodes[p].writes & L.nodes[o].writes & renameMask;
            if (!both || t[p] % II != t[o] % II) continue;
            for (int r = 0; r < 64; r++) if (both & (1ull << r)) { std::string name = std::string(r < 32 ? "A" : "B") + std::to_string(r % 32); minQ[name] = std::max(minQ[name], std::abs(t[o] - t[p]) / II + 1); }
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
        long by;
        bool rmw = l.mnem == "MVKH" || l.mnem == "ADDK" || !l.pred.empty() || !steppedRegister(l, by).empty();
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
        if (carried || (L.liveExit & bitOf(r)) || r == L.iv || r == L.bound || r == L.C || r == L.P || L.pinned(r)) webs[static_cast<std::size_t>(it->second)].keep = true;
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
        long by;
        const std::string stepped = steppedRegister(L.body[o], by);
        for (std::size_t w = 0; w < writes.size(); w++) {
            if (!seen.count(writes[w]) && (L.body[o].mnem == "MVKH" || L.body[o].mnem == "ADDK")) L.rmwFirst.insert(writes[w]);
            if (writes[w] == stepped) L.rmwFirst.insert(writes[w]);
            seen.insert(writes[w]);
        }
    }
}

// Whether register r is read before it is written on the path from line k round the back edge to line o - the
// value r holds after line o is wanted - or is live at the exit.
bool wantedAfter(const Loop &L, std::size_t o, const std::string &r) {
    const std::vector<Line> &b = L.body;
    for (std::size_t n = 0, k = o + 1; n < b.size(); n++, k = (k + 1) % b.size()) {
        std::vector<std::string> reads, writes;
        readsAndWrites(b[k], reads, writes);
        if (has(reads, r)) return true;
        if (has(writes, r)) return false;
    }
    return true;
}

// **The body's own copies**: `MV S, D` copied straight back by `MV D, S` with neither written between is one copy, and
// a copy nothing reads before the register is written again - round the back edge too, the exit not reading it - is none.
// The backend saves a result across the address it computes next, and the save is a web the schedule would carry.
void foldBodyCopies(Loop &L) {
    std::vector<Line> &b = L.body;
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t o = 0; o < b.size() && !changed; o++) {
            if (b[o].mnem != "MV" || b[o].ops.size() != 2 || !b[o].pred.empty() || !sideOf(b[o].ops[0]) || !sideOf(b[o].ops[1])) continue;
            const std::string S = b[o].ops[0], D = b[o].ops[1];
            if (D == "A15" || D == "B15" || D == "B3" || D == "B14" || D == L.iv || D == L.bound || D == L.P) continue;
            for (std::size_t k = o + 1; k < b.size(); k++) {
                std::vector<std::string> reads, writes;
                readsAndWrites(b[k], reads, writes);
                if (b[k].mnem == "MV" && b[k].ops.size() == 2 && b[k].pred.empty() && b[k].ops[0] == D && b[k].ops[1] == S) { b.erase(b.begin() + static_cast<long>(k)); changed = true; break; }
                if (has(writes, S) || has(writes, D)) break;
            }
            if (changed) break;
            if ((L.liveExit & bitOf(D)) || wantedAfter(L, o, D)) continue;
            b.erase(b.begin() + static_cast<long>(o));
            changed = true;
        }
    }
}

// A legal line, or false: the stepped guard is made of ordinary instructions and every one must have a form.
bool emitLegal(std::vector<Line> &out, Line l, std::string &why) {
    if (!legalForm(l)) { why = "the stepped guard has no legal form for" + l.raw; return false; }
    out.push_back(l);
    return true;
}

// A register of free's on the side asked for, or any; "" for none. With pair, an aligned even:odd pair's even half.
std::string takeFree(std::vector<std::string> &free, char side, bool pair) {
    for (int pass = 0; pass < 2; pass++)
        for (std::size_t f = 0; f < free.size(); f++) {
            if (pass == 0 && sideOf(free[f]) != side) continue;
            if (!pair) { std::string r = free[f]; free.erase(free.begin() + static_cast<long>(f)); return r; }
            int num = std::atoi(free[f].c_str() + 1);
            if (num % 2) continue;
            std::string hi = std::string(1, free[f][0]) + std::to_string(num + 1);
            std::vector<std::string>::iterator ih = std::find(free.begin(), free.end(), hi);
            if (ih == free.end()) continue;
            std::string r = free[f];
            free.erase(ih); free.erase(free.begin() + static_cast<long>(f));
            return r;
        }
    return "";
}

// The loop's own test in one canonical shape: P = iv < n where the loop runs while iv < n, P = n < iv where it runs
// while iv <= n; a two-word counter signed through the high word and unsigned through the low, over two scratch registers.
bool steppedTest(const Loop &L, std::vector<Line> &out, const std::string &nlo, const std::string &nhi, const std::string &t, const std::string &tb, std::string &why) {
    const bool less = !L.plusOne;
    const std::string &lo = L.ivs[0];
    const bool across = sideOf(L.P) != sideOf(lo);    // the result is made beside the counter and copied to P
    if (!L.wide) {
        if (!across) return emitLegal(out, make(less ? "CMPLT" : "CMPGT", lo, nlo, L.P), why);
        if (t.empty()) { why = "the stepped test has no scratch register beside the counter"; return false; }
        return emitLegal(out, make(less ? "CMPLT" : "CMPGT", lo, nlo, t), why) && emitLegal(out, make("MV", t, L.P), why);
    }
    const std::string &hi = L.ivs[1], &x = across ? tb : L.P;
    return emitLegal(out, make("CMPEQ", hi, nhi, t), why) && emitLegal(out, make(less ? "CMPLTU" : "CMPGTU", lo, nlo, tb), why) &&
           emitLegal(out, make("AND", t, tb, t), why) && emitLegal(out, make(less ? "CMPLT" : "CMPGT", hi, nhi, tb), why) &&
           emitLegal(out, make("OR", t, tb, x), why) && (!across || emitLegal(out, make("MV", x, L.P), why));
}

// **The stepped loop's entry**: the step must not be negative, K = m*s and Kg = g*s must fit, n-K (the kernel's bound)
// and n-Kg must not overflow, and turn g must exist - `iv REL n-Kg` by the loop's own test - or the loop runs as written.
// Every check branches to the head, and the test is the loop's own over the bound n - Kg; one or two words.
bool steppedGuard(const Loop &L, std::vector<Line> &out, const std::vector<std::string> &moved, const std::string &xt, const std::string &xtb,
                  std::vector<std::string> free, int m, int g, std::string &why) {
    const std::string &slo = L.steps[0], &nlo = L.bounds[0];
    const std::string shi = L.wide ? L.steps[1] : "", nhi = L.wide ? L.bounds[1] : "";
    const bool constant = isNumber(slo);
    const char stepSide = constant ? sideOf(nlo) : sideOf(slo);
    const std::string klo = takeFree(free, stepSide, true), tm = takeFree(free, stepSide, false), t2 = takeFree(free, stepSide, false), tb = takeFree(free, sideOf(nlo), false);
    if (klo.empty() || tm.empty() || t2.empty() || tb.empty()) { why = "no free registers for the stepped guard"; return false; }
    const std::string khi = std::string(1, klo[0]) + std::to_string(std::atoi(klo.c_str() + 1) + 1);
    const std::string skip = L.negated ? L.P : "!" + L.P;    // the branch past the pipelined copy, taken where the loop would not go on
    Line toHead = rebuilt("B", std::vector<std::string>(1, L.label), L.P);
    if (!constant && !emitLegal(out, make("CMPGT", "0", L.wide ? shi : slo, L.P), why)) return false;
    if (!constant) out.push_back(toHead);
    // K = k*s into khi:klo, checked to fit: a 31-bit value for one word, a 47-bit step for two.
    auto times = [&](int k) -> bool {
        if (constant) {
            long kk = k * std::atol(slo.c_str());
            if (kk < 0 || kk > 0x7fffffff) { why = "the stepped guard's constant does not fit"; return false; }
            if (kk <= 32767) out.push_back(make("MVK", std::to_string(kk), klo));
            else { out.push_back(make("MVKL", std::to_string(kk), klo)); out.push_back(make("MVKH", std::to_string(kk), klo)); }
            return true;
        }
        out.push_back(make("MVK", std::to_string(k), tm));
        if (!emitLegal(out, make("MPY32U", slo, tm, khi + ":" + klo), why)) return false;
        if (!L.wide) {
            if (!emitLegal(out, make("CMPGT", "0", klo, tb), why) || !emitLegal(out, make("XOR", "1", tb, tb), why)) return false;
            if (!emitLegal(out, make("CMPEQ", "0", khi, L.P), why) || !emitLegal(out, make("AND", L.P, tb, L.P), why)) return false;
            out.push_back(rebuilt("B", std::vector<std::string>(1, L.label), "!" + L.P));
            return true;
        }
        if (!emitLegal(out, make("MPY32", shi, tm, t2), why) || !emitLegal(out, make("ADD", t2, khi, khi), why)) return false;
        if (!emitLegal(out, make("SHRU", shi, "15", t2), why) || !emitLegal(out, make("CMPEQ", "0", t2, L.P), why)) return false;
        out.push_back(rebuilt("B", std::vector<std::string>(1, L.label), "!" + L.P));
        return true;
    };
    // d = n - (khi:klo), into dlo (and dhi), then the overflow check: n negative and the difference not.
    auto minus = [&](const std::string &dlo, const std::string &dhi) -> bool {
        if (!L.wide) {
            if (!emitLegal(out, make("SUB", nlo, klo, dlo), why)) return false;
            if (!emitLegal(out, make("CMPGT", "0", nlo, L.P), why) || !emitLegal(out, make("CMPGT", "0", dlo, tb), why)) return false;
        } else {
            if (!emitLegal(out, make("CMPLTU", nlo, klo, tb), why) || !emitLegal(out, make("SUB", nlo, klo, dlo), why)) return false;
            if (!emitLegal(out, make("SUB", nhi, khi, dhi), why) || !emitLegal(out, make("SUB", dhi, tb, dhi), why)) return false;
            if (!emitLegal(out, make("CMPGT", "0", nhi, L.P), why) || !emitLegal(out, make("CMPGT", "0", dhi, tb), why)) return false;
        }
        if (!emitLegal(out, make("XOR", "1", tb, tb), why) || !emitLegal(out, make("AND", L.P, tb, L.P), why)) return false;
        out.push_back(toHead);
        return true;
    };
    if (!times(m) || !minus(moved[0], L.wide ? moved[1] : "")) return false;
    if (g != m && !times(g)) return false;
    if (!minus(klo, khi)) return false;
    // The entry test over the bound n - Kg, then the branch past the copy where it fails.
    if (!steppedTest(L, out, klo, khi, xt, xtb, why)) return false;
    out.push_back(rebuilt("B", std::vector<std::string>(1, L.label), skip));
    return true;
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
    if (!L.stepped) {
        for (std::size_t k = 0; k < pool.size() && X.empty(); k++) if (sideOf(pool[k]) == sideOf(L.C)) X = pool[k];
        if (X.empty()) { why = "no free register for the count"; return false; }
        pool.erase(std::find(pool.begin(), pool.end(), X));
    }
    // A stepped counter's kernel tests the moved bound: a fresh register per word, taking the bound's own side.
    std::vector<std::string> moved;
    for (std::size_t w = 0; w < L.bounds.size(); w++) {
        std::string f;
        for (int pass = 0; pass < 2 && f.empty(); pass++)
            for (std::size_t k = 0; k < pool.size() && f.empty(); k++)
                if (pass == 1 || sideOf(pool[k]) == sideOf(L.bounds[w])) { f = pool[k]; pool.erase(pool.begin() + static_cast<long>(k)); }
        if (f.empty()) { why = "no free register for the moved bound"; return false; }
        moved.push_back(f);
    }
    // The test's scratch registers, beside the counter so that only the bound crosses: taken before the renaming can.
    std::string xt, xtb;
    if (L.stepped && (L.wide || sideOf(L.P) != sideOf(L.ivs[0]))) {
        xt = takeFree(pool, sideOf(L.ivs[0]), false);
        if (L.wide) xtb = takeFree(pool, sideOf(L.ivs[0]), false);
        if (xt.empty() || (L.wide && xtb.empty()) || sideOf(xt) != sideOf(L.ivs[0]) || (L.wide && sideOf(xtb) != sideOf(L.ivs[0]))) {
            why = "no free register beside the counter for the stepped test"; return false;
        }
    }
    for (std::size_t k = 0; k < L.testBody.size(); k++) {
        Line &l = L.body[L.testBody[k]];
        std::vector<std::string> ops = l.ops;
        for (std::size_t o = 0; o + 1 < ops.size(); o++)
            for (std::size_t w = 0; w < L.bounds.size(); w++) if (ops[o] == L.bounds[w]) ops[o] = moved[w];
        l = rebuilt(l.mnem, ops, l.pred);
    }
    foldBodyCopies(L);
    if (tracing() > 4) for (std::size_t o = 0; o < L.body.size(); o++) std::fprintf(stderr, "  as written%s\n", L.body[o].raw.c_str());
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
            if (L.predicated.count(r) || L.rmwFirst.count(r) || r == L.C || r == L.P || r == "A15" || r == "B15" || r == "B3" || r == "B14") { ok = false; break; }
        }
        if (ok) for (std::size_t h = 0; h < units[u].regs.size(); h++) renameable.insert(units[u].regs[h]);
    }
    if (tracing() > 3) for (std::size_t o = 0; o < L.body.size(); o++) std::fprintf(stderr, "  body%s\n", L.body[o].raw.c_str());
    if (tracing() > 2) { std::fprintf(stderr, "  renameable:"); for (std::set<std::string>::const_iterator it = renameable.begin(); it != renameable.end(); ++it) std::fprintf(stderr, " %s", it->c_str()); std::fprintf(stderr, "\n"); }
    std::vector<int> t;
    std::map<std::string, int> minQ;
    std::vector<std::string> free;          // the pool less the copies: what a guard may still take
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
                if ((L.liveExit & bit) || it->first == L.iv || it->first == L.bound || L.pinned(it->first)) un.exits = true;
            }
            for (std::size_t x = 0; x < units.size(); x++) qMax = std::max(qMax, units[x].q);
            u = pow2At(std::max((6 + II - 1) / II, qMax));
            // The copies: the register itself, then q-1 more of its side - a pair's two halves aligned.
            free = pool;
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
            if (tracing() > 1) std::fprintf(stderr, "  II=%d: S=%d, no %d copies of %s - trying II+1\n", II, S, units[unitOf[failed[0]]].q, failed[0].c_str());
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
    // A stepped counter: the kernel's branch, at cycle u*II-6 of a round, reads the test iteration k_b made of the counter
    // one turn on, so the bound it tests is moved back by m = 2u-2-d steps (d the turns between that test and the branch);
    // a round is entered only when its last-started turn exists, and the loop is entered when turns 0..u+S-2 do.
    int m = 0, g = 0;
    if (L.stepped) {
        int tP = 0;
        for (std::size_t o = 0; o < L.nodes.size(); o++) if (L.nodes[o].writes & bitOf(L.P)) tP = t[o];
        auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        int d = floorDiv(u * II - 7 - tP, II);
        while (S - 1 + d < 0) { u *= 2; d = floorDiv(u * II - 7 - tP, II); }   // the first round's branch must see a test
        m = 2 * u - 2 - d; g = u + S - 2;
    }
    if (tracing()) std::fprintf(stderr, "pipe %s: II=%d S=%d u=%d seq=%d ops=%zu%s\n", L.label.c_str(), II, S, u, seq, L.nodes.size(), L.stepped ? (L.wide ? " stepped wide" : " stepped") : "");
    if (L.stepped) { if (!steppedGuard(L, out, moved, xt, xtb, free, m, g, why)) return false; }
    // The guard: X = (n - i [+1] - (S-1)) >> log2 u, the unrolled rounds; none, and the loop is left as written.
    const bool crossBoth = !L.stepped && !L.boundConst && sideOf(L.bound) != sideOf(X) && sideOf(L.iv) != sideOf(X);
    if (L.stepped) { }
    else if (L.boundConst) {
        if (L.boundK >= -32768 && L.boundK <= 32767) out.push_back(make("MVK", std::to_string(L.boundK), X));
        else { out.push_back(make("MVKL", std::to_string(L.boundK), X)); out.push_back(make("MVKH", std::to_string(L.boundK), X)); }
        out.push_back(make("SUB", X, L.iv, X));
    } else if (crossBoth) { out.push_back(make("MV", L.bound, X)); out.push_back(make("SUB", X, L.iv, X)); }
    else out.push_back(make("SUB", L.bound, L.iv, X));
    if (!L.stepped) {
        if (L.plusOne) out.push_back(make("ADD", "1", X, X));
        if (S > 1) { if (S - 1 <= 31) out.push_back(make("SUB", X, std::to_string(S - 1), X)); else out.push_back(make("ADDK", std::to_string(1 - S), X)); }
        int log2u = 0;
        while ((1 << log2u) < u) log2u++;
        if (log2u) out.push_back(make("SHR", X, std::to_string(log2u), X));
        out.push_back(make("CMPGT", "1", X, L.P));
        out.push_back(rebuilt("B", std::vector<std::string>(1, L.label), L.P));
        out.push_back(make("SUB", X, "1", L.C));
    }
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
            if (c * II + s == u * II - 6) {
                p.push_back(rebuilt("B", std::vector<std::string>(1, L.label + "$pipe"), L.stepped ? (L.negated ? "!" + L.P : L.P) : L.C).raw);
                if (!L.stepped) p.push_back(make("ADD", "-1", L.C, L.C).raw);
            }
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
    if (L.stepped) { if (!steppedTest(L, out, L.bounds[0], L.wide ? L.bounds[1] : "", xt, xtb, why)) return false; }
    else out.push_back(v[L.cmp]);
    out.push_back(rebuilt("B", std::vector<std::string>(1, L.exitName), L.negated ? L.P : "!" + L.P));
    return true;
}


// ----- a loop that leaves on a byte it reads: eight turns over one aligned 8-byte block -----

// **A loop whose exit hangs on what it loads** - `while (*p) p++`, strchr, strcmp, a string hash - is unrolled eight
// turns: each turn's arithmetic runs ahead under fresh names and only a value the loop leaves behind is copied back,
// under R, the mask of the turns still alive. No turn the loop would not have run writes anything it can see.
struct Window {
    struct Ptr {
        std::string reg;
        std::map<std::size_t, long> loads;      // body index -> the byte it reads, past the turn's own value of reg
        long offmin = 0, offmax = 0;
        bool cond = false;                      // the first byte read only after an exit: the block is read under G
        std::size_t anchor = 0;
    };
    struct Exit { std::string target, pred; std::uint64_t live = 0; };  // target "" for the fall-through after the back branch
    std::size_t head = 0, back = 0;
    std::string label, backPred, P;             // backPred "" for an unconditional back branch
    std::vector<Line> body;                     // the instructions in order, the back branch left out
    std::vector<int> cls;                       // a branch's exit, an instruction under one's predicate that exit, else kPlain or kCont
    std::vector<Exit> exits;                    // the exits inside, in order, then the back branch's fall-through
    std::uint64_t liveHead = 0, liveOut = 0;    // live at the head; live where any exit goes
    std::vector<Ptr> ptrs;
};
const int kPlain = -1, kCont = -2;

// A byte load's address as base and constant: `*R`, `*+R(c)`, `*-R(c)`, `*+R[c]`, `*R++`, `*R++(1)`; post for the two last.
bool byteAddress(const std::string &op, std::string &base, long &off, bool &post) {
    post = false; off = 0;
    if (op.size() < 3 || op[0] != '*') return false;
    std::string s = op.substr(1);
    char sign = 0;
    if (s[0] == '+' || s[0] == '-') { if (s.size() > 1 && (s[1] == '+' || s[1] == '-')) return false; sign = s[0]; s = s.substr(1); }
    std::size_t i = 0;
    while (i < s.size() && std::isalnum(static_cast<unsigned char>(s[i]))) i++;
    base = s.substr(0, i);
    if (!sideOf(base)) return false;
    const std::string rest = s.substr(i);
    if (rest.empty()) return sign == 0;
    if (rest == "++" || rest == "++(1)") { post = true; return sign == 0; }
    if (!sign || rest.size() < 3 || !((rest[0] == '(' && rest.back() == ')') || (rest[0] == '[' && rest.back() == ']'))) return false;
    const std::string k = rest.substr(1, rest.size() - 2);
    if (!isNumber(k)) return false;
    off = std::atol(k.c_str()) * (sign == '-' ? -1 : 1);
    return true;
}

bool isByteLoad(const std::string &m) { return m == "LDB" || m == "LDBU"; }

// Pointer r followed through one turn: registers holding r plus a constant, through copies, constant adds and the
// steps of byte loads; true where r ends the turn one past where it began, with each byte load's offset in loads.
bool trackPointer(const Window &W, const std::string &r, std::map<std::size_t, long> &loads) {
    std::map<std::string, long> sym;
    sym[r] = 0;
    for (std::size_t k = 0; k < W.body.size(); k++) {
        const Line &l = W.body[k];
        if (isBranch(l.mnem)) continue;
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        if (W.cls[k] >= 0) {                    // done only on the way out: what stays on sees none of it
            std::vector<std::string> regs;
            if (isLoad(l.mnem)) registersIn(l.ops[0], regs);
            for (std::size_t g = 0; g < regs.size(); g++) if (sym.count(regs[g])) return false;
            continue;
        }
        if (isLoad(l.mnem)) {
            std::string base; long off; bool post;
            std::vector<std::string> regs;
            registersIn(l.ops[0], regs);
            bool touches = false;
            for (std::size_t g = 0; g < regs.size(); g++) if (sym.count(regs[g])) touches = true;
            if (touches) {
                if (!isByteLoad(l.mnem) || !byteAddress(l.ops[0], base, off, post) || !sym.count(base)) return false;
                loads[k] = sym[base] + off;
                if (post) sym[base] += 1;
            }
            sym.erase(l.ops.back());
            continue;
        }
        if (writes.empty()) continue;
        const std::string &d = writes[0];
        long v = 0; bool known = false;
        if (l.mnem == "MV" && l.ops.size() == 2 && sym.count(l.ops[0])) { v = sym[l.ops[0]]; known = true; }
        else if ((l.mnem == "ADD" || l.mnem == "SUB") && l.ops.size() == 3) {
            const bool sub = l.mnem == "SUB";
            if (isNumber(l.ops[0]) && sym.count(l.ops[1]) && !sub) { v = sym[l.ops[1]] + std::atol(l.ops[0].c_str()); known = true; }
            else if (isNumber(l.ops[1]) && sym.count(l.ops[0])) { v = sym[l.ops[0]] + (sub ? -1 : 1) * std::atol(l.ops[1].c_str()); known = true; }
        }
        if (known) sym[d] = v; else sym.erase(d);
    }
    return sym.count(r) && sym[r] == 1;
}

bool recogniseWindow(const std::vector<Line> &v, std::size_t back, const std::set<std::string> &labels, const std::set<std::string> &named, Window &W, std::string &why) {
    const Line &b = v[back];
    if (b.mnem != "B" || b.ops.empty() || labels.count(b.ops[0]) == 0) { why = "no branch back to a label"; return false; }
    W.label = b.ops[0]; W.backPred = b.pred; W.P = b.pred.empty() ? "" : b.pred[0] == '!' ? b.pred.substr(1) : b.pred; W.back = back;
    std::map<std::string, std::size_t> at;
    for (std::size_t i = 0; i < v.size(); i++) if (isLabel(v[i])) at[labelName(v[i])] = i;
    bool found = false;
    for (std::size_t i = 0; i < back; i++) if (isLabel(v[i]) && labelName(v[i]) == W.label) { W.head = i; found = true; }
    if (!found) { why = "the head is not above"; return false; }
    int names = 0;
    for (std::size_t i = 0; i < v.size(); i++) if (v[i].instr) for (std::size_t o = 0; o < v[i].ops.size(); o++) if (v[i].ops[o] == W.label) names++;
    if (names != 1) { why = "the head is entered from elsewhere"; return false; }
    W.liveHead = v[W.head].liveIn;
    for (std::size_t i = W.head + 1; i < back; i++) {
        const Line &l = v[i];
        if (isLabel(l)) { if (!passThrough(l, named)) { why = "a label inside"; return false; } continue; }
        if (!l.instr) { why = "a directive inside"; return false; }
        if (isBranch(l.mnem)) {
            if (l.mnem != "B" || l.pred.empty() || l.ops.empty() || !at.count(l.ops[0]) || l.ops[0] == W.label) { why = "a branch inside that is not a way out"; return false; }
            Window::Exit e; e.target = l.ops[0]; e.pred = l.pred;
            if (!W.backPred.empty() && back + 1 < v.size() && isLabel(v[back + 1]) && labelName(v[back + 1]) == e.target) e.target = "";
            e.live = v[at[l.ops[0]]].liveIn;
            W.cls.push_back(static_cast<int>(W.exits.size()));
            W.exits.push_back(e);
            W.liveOut |= e.live;
            W.body.push_back(l);
            continue;
        }
        if (isStore(l.mnem)) { why = "a store, which a byte read early might meet"; return false; }
        if (l.mnem == "ADDK" || l.mnem == "MVKH" || l.mnem == "MVKL") { why = "an instruction that reads its destination"; return false; }
        for (std::size_t o = 0; o < l.ops.size(); o++) if (l.ops[o].find(':') != std::string::npos) { why = "a register pair"; return false; }
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        if (has(writes, "A15") || has(writes, "B15") || has(writes, "B3")) { why = "the stack pointer or return address is written"; return false; }
        if (!isLoad(l.mnem) && (unitsFor(l) == 0 || writes.size() != 1)) { why = "an instruction of no known form: " + l.mnem; return false; }
        W.cls.push_back(kPlain);
        W.body.push_back(l);
    }
    if (!W.backPred.empty()) {
        Window::Exit e; e.pred = W.backPred[0] == '!' ? W.P : "!" + W.P;
        e.live = back + 1 < v.size() ? v[back + 1].liveIn : ~0ull;
        W.exits.push_back(e);
        W.liveOut |= e.live;
    }
    if (W.exits.empty()) { why = "no way out"; return false; }
    if (W.body.empty() || W.body.size() > 20) { why = "too small or too large to unroll"; return false; }
    // An instruction under a predicate belongs to the next branch, under the same one: done on the way out there,
    // or, before the back branch, only where the turn goes on. Its predicate must not change on the way.
    for (std::size_t i = 0; i < W.body.size(); i++) {
        const Line &l = W.body[i];
        if (l.pred.empty() || isBranch(l.mnem)) continue;
        const std::string q = l.pred[0] == '!' ? l.pred.substr(1) : l.pred;
        std::size_t j = i + 1;
        for (; j < W.body.size(); j++) {
            if (isBranch(W.body[j].mnem)) break;
            std::vector<std::string> reads, writes;
            readsAndWrites(W.body[j], reads, writes);
            if (has(writes, q)) { why = "a predicate written under its own instructions"; return false; }
        }
        if (j < W.body.size() && W.body[j].pred == l.pred) W.cls[i] = W.cls[j];
        else if (j == W.body.size() && l.pred == W.backPred) W.cls[i] = kCont;
        else { why = "an instruction under a predicate no branch after it takes"; return false; }
    }
    // The pointers stepped by one a turn, followed through copies and constant offsets to the byte loads that read them.
    std::set<std::string> tried;
    for (std::size_t k = 0; k < W.body.size(); k++) {
        std::vector<std::string> reads, writes;
        readsAndWrites(W.body[k], reads, writes);
        for (std::size_t r = 0; r < reads.size(); r++) {
            if (!(W.liveHead & bitOf(reads[r])) || !tried.insert(reads[r]).second) continue;
            Window::Ptr p;
            p.reg = reads[r];
            if (!trackPointer(W, p.reg, p.loads) || p.loads.empty()) continue;
            bool first = true, exited = false;
            for (std::size_t i = 0; i < W.body.size(); i++) {
                if (isBranch(W.body[i].mnem)) { exited = true; continue; }
                std::map<std::size_t, long>::const_iterator it = p.loads.find(i);
                if (it == p.loads.end()) continue;
                if (sideOf(W.body[i].ops.back()) != sideOf(p.reg)) { first = false; p.loads.clear(); break; }
                if (first || it->second < p.offmin) { p.offmin = it->second; p.anchor = i; p.cond = exited; }
                if (first || it->second > p.offmax) p.offmax = it->second;
                first = false;
            }
            if (p.loads.empty()) continue;
            if (p.offmax - p.offmin > 4) { why = "the bytes of one turn lie too far apart"; return false; }
            W.ptrs.push_back(p);
        }
    }
    if (W.ptrs.empty()) { why = "no byte load through a pointer stepped by one"; return false; }
    if (W.ptrs.size() > 3) { why = "more pointers than the block reads can carry"; return false; }
    // A load through any other register is read where it stands, under R: it must not step its register.
    for (std::size_t k = 0; k < W.body.size(); k++) {
        const Line &l = W.body[k];
        if (!isLoad(l.mnem)) continue;
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        bool stepped = false;
        for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].loads.count(k)) stepped = true;
        if (!stepped && writes.size() != 1) { why = "a load that steps its own register"; return false; }
    }
    why.clear();
    return true;
}

// The window's code before its registers are given: a virtual register is `%N`, a physical one its own name.
struct WinCode {
    struct Op { std::string mnem, pred; std::vector<std::string> ops; };
    std::vector<Op> ops;
    std::vector<char> side;
    std::set<int> boolean;                  // made by a compare: 0 or 1
    std::map<std::string, std::pair<std::string, long> > affine;   // a register as another plus a constant
    std::string fresh(char s) { side.push_back(s); return "%" + std::to_string(side.size() - 1); }
    void put(const std::string &m, const std::vector<std::string> &o, const std::string &pred = "") { Op x; x.mnem = m; x.ops = o; x.pred = pred; ops.push_back(x); }
    bool isBool(const std::string &r) const { return r[0] == '%' && boolean.count(std::atoi(r.c_str() + 1)); }
    std::pair<std::string, long> base(const std::string &r) const {
        std::map<std::string, std::pair<std::string, long> >::const_iterator it = affine.find(r);
        return it == affine.end() ? std::make_pair(r, 0L) : it->second;
    }
};

char sideOfAny(const WinCode &c, const std::string &r) { return r[0] == '%' ? c.side[static_cast<std::size_t>(std::atoi(r.c_str() + 1))] : sideOf(r); }

// Every register of an operand renamed by cur, those not in it left as they are.
std::string renamedBy(const std::string &op, const std::map<std::string, std::string> &cur) {
    std::vector<std::string> regs;
    registersIn(op, regs);
    std::string s = op;
    for (std::size_t r = 0; r < regs.size(); r++) {
        std::map<std::string, std::string>::const_iterator it = cur.find(regs[r]);
        if (it != cur.end() && it->second != regs[r]) s = renamed(s, regs[r], it->second);
    }
    return s;
}

std::string byteOperand(const std::string &base, long off) {
    if (off == 0) return "*" + base;
    return std::string("*") + (off > 0 ? "+" : "-") + base + "(" + std::to_string(off < 0 ? -off : off) + ")";
}

// The window's code with registers given: an instruction whose result nothing reads is dropped first, then each
// virtual register takes a physical one of its side in the order the code reads, freed after its last reader.
bool giveRegisters(WinCode &c, std::vector<std::string> pool[2], std::vector<Line> &code, std::string &why) {
    auto each = [&](const std::string &op, const std::function<void(int)> &f) {
        for (std::size_t i = 0; i < op.size(); i++)
            if (op[i] == '%') { std::size_t j = i + 1; while (j < op.size() && std::isdigit(static_cast<unsigned char>(op[j]))) j++; f(std::atoi(op.c_str() + i + 1)); i = j - 1; }
    };
    for (bool changed = true; changed;) {
        changed = false;
        std::set<int> read;
        for (std::size_t i = 0; i < c.ops.size(); i++)
            for (std::size_t o = 0; o < c.ops[i].ops.size(); o++)
                if (o + 1 < c.ops[i].ops.size() || c.ops[i].ops[o][0] == '*') each(c.ops[i].ops[o], [&](int n) { read.insert(n); });
        for (std::size_t i = c.ops.size(); i-- > 0;) {
            const std::string &d = c.ops[i].ops.back();
            if (d[0] == '%' && d.find_first_not_of("0123456789", 1) == std::string::npos && !read.count(std::atoi(d.c_str() + 1))) {
                c.ops.erase(c.ops.begin() + static_cast<long>(i));
                changed = true;
            }
        }
    }
    // Registers given in the order the code reads, each freed after its last reader, oldest freed first.
    std::vector<int> last(c.side.size(), -1);
    for (std::size_t i = 0; i < c.ops.size(); i++) for (std::size_t o = 0; o < c.ops[i].ops.size(); o++) each(c.ops[i].ops[o], [&](int n) { last[static_cast<std::size_t>(n)] = static_cast<int>(i); });
    std::vector<std::string> phys(c.side.size());
    std::vector<bool> given(c.side.size(), false);
    std::deque<std::string> freeList[2];
    for (int s = 0; s < 2; s++) for (std::size_t k = 0; k < pool[s].size(); k++) freeList[s].push_back(pool[s][k]);
    std::vector<int> live;
    for (std::size_t i = 0; i < c.ops.size(); i++) {
        for (std::size_t a = 0; a < live.size();) {
            if (last[static_cast<std::size_t>(live[a])] < static_cast<int>(i)) { const std::string &r = phys[static_cast<std::size_t>(live[a])]; freeList[sideIndex(r)].push_back(r); live.erase(live.begin() + static_cast<long>(a)); }
            else a++;
        }
        bool ok = true;
        for (std::size_t o = 0; o < c.ops[i].ops.size(); o++) each(c.ops[i].ops[o], [&](int n) {
            if (given[static_cast<std::size_t>(n)]) return;
            const int s = c.side[static_cast<std::size_t>(n)] == 'B' ? 1 : 0;
            if (freeList[s].empty()) { ok = false; return; }
            phys[static_cast<std::size_t>(n)] = freeList[s].front(); freeList[s].pop_front();
            given[static_cast<std::size_t>(n)] = true; live.push_back(n);
        });
        if (!ok) { why = "no registers for the turns"; return false; }
    }
    for (std::size_t i = 0; i < c.ops.size(); i++) {
        std::vector<std::string> ops = c.ops[i].ops;
        for (std::size_t o = 0; o < ops.size(); o++) {
            std::string s;
            for (std::size_t j = 0; j < ops[o].size(); j++) {
                if (ops[o][j] != '%') { s += ops[o][j]; continue; }
                std::size_t e = j + 1;
                while (e < ops[o].size() && std::isdigit(static_cast<unsigned char>(ops[o][e]))) e++;
                s += phys[static_cast<std::size_t>(std::atoi(ops[o].c_str() + j + 1))];
                j = e - 1;
            }
            ops[o] = s;
        }
        Line l = rebuilt(c.ops[i].mnem, ops, c.ops[i].pred);
        if (!isLoad(l.mnem) && !legalForm(l)) { why = "no legal form for" + l.raw; return false; }
        code.push_back(l);
    }
    return true;
}

// ----- the closed form: where all a loop leaves behind is its registers plus a constant step a turn -----

// A value within one turn as a register's value at the turn's start plus a constant; base "" for a constant alone.
struct Aff { std::string base; long c = 0; bool ok = false; };

// **One turn read as affine values**: each value the loop leaves - at each exit where it is live, and at the head - must
// be the start value of a register that steps by -1, 0 or 1 a turn, plus a constant; then the turn a window stops in
// says them all, and no turn writes anything. at[j] is exit j's state, step a register's per-turn step.
bool affineTurn(const Window &W, const std::set<std::string> &committed, std::vector<std::map<std::string, Aff> > &at, std::map<std::string, long> &step) {
    std::map<std::string, Aff> st, away;
    auto get = [&](const std::string &r) { if (away.count(r)) return away[r]; if (st.count(r)) return st[r]; Aff a; a.base = r; a.ok = true; return a; };
    auto value = [&](const Line &l) {
        Aff a;
        if (l.mnem == "MV" && l.ops.size() == 2) return get(l.ops[0]);
        if (l.mnem == "MVK" && l.ops.size() == 2 && isNumber(l.ops[0])) { a.ok = true; a.c = std::atol(l.ops[0].c_str()); return a; }
        if ((l.mnem == "ADD" || l.mnem == "SUB") && l.ops.size() == 3) {
            int ki = isNumber(l.ops[0]) && l.mnem == "ADD" ? 0 : isNumber(l.ops[1]) ? 1 : -1;
            if (ki >= 0 && !isNumber(l.ops[static_cast<std::size_t>(1 - ki)])) {
                a = get(l.ops[static_cast<std::size_t>(1 - ki)]);
                a.c += std::atol(l.ops[static_cast<std::size_t>(ki)].c_str()) * (l.mnem == "SUB" ? -1 : 1);
                return a;
            }
        }
        return a;
    };
    at.assign(W.exits.size(), std::map<std::string, Aff>());
    bool finalTaken = false;
    for (std::size_t i = 0; i < W.body.size(); i++) {
        const Line &l = W.body[i];
        if (isBranch(l.mnem)) {
            for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) at[static_cast<std::size_t>(W.cls[i])][*r] = get(*r);
            away.clear();
            continue;
        }
        if (W.cls[i] == kCont && !finalTaken && !W.backPred.empty()) {
            for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) at.back()[*r] = get(*r);
            finalTaken = true;
        }
        std::vector<std::string> reads, writes;
        readsAndWrites(l, reads, writes);
        if (writes.empty()) continue;
        const Aff a = isLoad(l.mnem) ? Aff() : value(l);
        if (W.cls[i] >= 0) away[writes[0]] = a; else st[writes[0]] = a;
    }
    if (!W.backPred.empty() && !finalTaken)
        for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) at.back()[*r] = get(*r);
    // The steps: a register live at the head ends the turn as itself plus its step.
    for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) {
        if (!(W.liveHead & bitOf(*r))) continue;
        const Aff e = get(*r);
        if (!e.ok || e.base != *r || e.c < -1 || e.c > 1) return false;
        step[*r] = e.c;
    }
    // Every value an exit needs, over a register of known step or none; a register the loop never writes steps by 0.
    for (std::size_t j = 0; j < W.exits.size(); j++)
        for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) {
            if (!(W.exits[j].live & bitOf(*r))) continue;
            const Aff &a = at[j][*r];
            if (!a.ok) return false;
            if (!a.base.empty() && !step.count(a.base)) { if (committed.count(a.base)) return false; step[a.base] = 0; }
        }
    return true;
}

bool emitClosed(Window &W, const std::set<std::string> &committed, std::vector<std::string> pool[2], std::vector<Line> &out, std::string &why) {
    std::vector<std::map<std::string, Aff> > at;
    std::map<std::string, long> step;
    const std::size_t E = W.exits.size();
    if (E > 2) { why = "more than two ways out of a turn"; return false; }
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].offmin != W.ptrs[p].offmax) { why = "bytes of a turn at two places"; return false; }
    for (std::size_t k = 0; k < W.body.size(); k++) {
        bool stepped = false;
        for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].loads.count(k)) stepped = true;
        if (isLoad(W.body[k].mnem) && !stepped) { why = "a load that cannot be read ahead"; return false; }
    }
    if (!affineTurn(W, committed, at, step)) { why = "a value left behind that is not a step a turn"; return false; }
    for (std::size_t p = 0; p < W.ptrs.size(); p++)
        if (W.ptrs[p].cond) {
            int before = 0;
            for (std::size_t i = 0; i < W.ptrs[p].anchor; i++) if (isBranch(W.body[i].mnem)) before++;
            if (before != 1) { why = "a block read behind more than one exit"; return false; }
        }
    auto isFree = [&](int s, int k) { return std::find(pool[s].begin(), pool[s].end(), std::string(s ? "B" : "A") + std::to_string(k)) != pool[s].end(); };
    auto takePred = [&]() -> std::string {
        for (int s = 0; s < 2; s++) for (int k = 0; k < 3; k++) if (isFree(s, k)) { std::string r = std::string(s ? "B" : "A") + std::to_string(k); pool[s].erase(std::find(pool[s].begin(), pool[s].end(), r)); return r; }
        return "";
    };
    const std::string Pc = takePred(), Pj = E > 1 ? takePred() : "";
    std::vector<std::string> G(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond) G[p] = takePred();
    bool ok = !Pc.empty() && (E == 1 || !Pj.empty());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond && G[p].empty()) ok = false;
    if (!ok) { why = "no condition registers for the closed form"; return false; }
    WinCode c;
    std::map<std::string, std::string> cur;
    // Each pointer's block and offset in it; the turns run to the end of the nearest block, w = 8 - offset.
    std::vector<std::string> B(W.ptrs.size()), O(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) {
        const char s = sideOf(W.ptrs[p].reg);
        std::string u = W.ptrs[p].reg;
        if (W.ptrs[p].offmin != 0) { u = c.fresh(s); c.put("ADD", { std::to_string(W.ptrs[p].offmin), W.ptrs[p].reg, u }); }
        O[p] = c.fresh(s); c.put("AND", { "7", u, O[p] });
        B[p] = c.fresh(s); c.put("AND", { "-8", u, B[p] });
    }
    std::map<std::string, std::string> loaded;
    auto loadWrapped = [&](std::size_t p) {
        const char s = sideOf(W.ptrs[p].reg);
        std::set<std::string> mn;
        for (std::map<std::size_t, long>::const_iterator it = W.ptrs[p].loads.begin(); it != W.ptrs[p].loads.end(); ++it) mn.insert(W.body[it->first].mnem);
        // With two pointers, the odd turns' bytes through a copy of the block on the other side: both .D units read.
        const char o2 = W.ptrs.size() < 2 ? s : s == 'A' ? 'B' : 'A';
        std::string B2 = B[p], O2 = O[p];
        if (o2 != s) { B2 = c.fresh(o2); O2 = c.fresh(o2); c.put("MV", { B[p], B2 }); c.put("MV", { O[p], O2 }); }
        for (int k = 1; k < 8; k++)
            for (std::set<std::string>::const_iterator m = mn.begin(); m != mn.end(); ++m) {
                const bool odd = k % 2 != 0;
                std::string t = c.fresh(odd ? o2 : s), x = c.fresh(odd ? o2 : s);
                c.put("ADD", { std::to_string(k), odd ? O2 : O[p], t });
                c.put("AND", { "7", t, t });
                c.put(*m, { "*+" + (odd ? B2 : B[p]) + "[" + t + "]", x }, G[p]);
                loaded[std::to_string(p) + "/" + std::to_string(k) + "/" + *m] = x;
            }
    };
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (!W.ptrs[p].cond) loadWrapped(p);
    auto valueOf = [&](const std::string &r) { return cur.count(r) ? cur[r] : r; };
    // Whether a turn stops at a test, as 0 or 1, from Q and the sense it leaves in: a compare's own result where it can be.
    auto stopOf = [&](const std::string &q, bool leavesWhenNonzero) {
        if (c.isBool(q) && leavesWhenNonzero) return q;
        std::string x = c.fresh(sideOfAny(c, q));
        if (c.isBool(q)) c.put("XOR", { "1", q, x });
        else if (leavesWhenNonzero) c.put("CMPLTU", { "0", q, x });
        else c.put("CMPEQ", { "0", q, x });
        c.boolean.insert(std::atoi(x.c_str() + 1));
        return x;
    };
    std::vector<std::string> dead;                      // d_p: 1 where some test up to place p stopped
    std::string d;
    auto pass = [&](int j, int k) {
        const std::string &pr = W.exits[static_cast<std::size_t>(j)].pred;
        const std::string q = pr[0] == '!' ? pr.substr(1) : pr;
        const std::string s = stopOf(valueOf(q), pr[0] != '!');
        if (d.empty()) d = s;
        else { std::string n = c.fresh(sideOfAny(c, d)); c.put("OR", { d, s, n }); d = n; }
        dead.push_back(d);
        if (k == 0 && j == 0)
            for (std::size_t p = 0; p < W.ptrs.size(); p++)
                if (W.ptrs[p].cond) { c.put("CMPEQ", { "0", s, G[p] }); loadWrapped(p); }
    };
    for (int k = 0; k < 8; k++) {
        for (std::size_t i = 0; i < W.body.size(); i++) {
            const Line &l = W.body[i];
            if (isBranch(l.mnem)) { pass(W.cls[i], k); continue; }
            if (W.cls[i] >= 0) continue;            // done on the way out: its value is read from the closed form
            const std::string &dst = l.ops.back();
            std::size_t ptr = W.ptrs.size();
            for (std::size_t p = 0; p < W.ptrs.size() && ptr == W.ptrs.size(); p++) if (W.ptrs[p].loads.count(i)) ptr = p;
            if (ptr < W.ptrs.size()) {
                std::string base; long off = 0; bool post = false;
                byteAddress(l.ops[0], base, off, post);
                const std::string key = std::to_string(ptr) + "/" + std::to_string(k) + "/" + l.mnem;
                if (!loaded.count(key)) {
                    std::string x = c.fresh(sideOf(dst));
                    c.put(l.mnem, { byteOperand(valueOf(base), post ? 0 : off), x }, W.ptrs[ptr].cond ? G[ptr] : "");
                    loaded[key] = x;
                }
                cur[dst] = loaded[key];
                if (post) { std::string nv = c.fresh(sideOf(base)); c.put("ADD", { "1", valueOf(base), nv }); cur[base] = nv; }
                continue;
            }
            if (l.mnem == "MV" && l.ops.size() == 2 && sideOf(l.ops[0]) && sideOfAny(c, valueOf(l.ops[0])) == sideOf(dst)) { cur[dst] = valueOf(l.ops[0]); continue; }
            std::vector<std::string> ops;
            for (std::size_t o = 0; o + 1 < l.ops.size(); o++) ops.push_back(renamedBy(l.ops[o], cur));
            std::string x = c.fresh(sideOf(dst));
            ops.push_back(x);
            c.put(l.mnem, ops);
            if (startsWith(l.mnem, "CMP")) c.boolean.insert(std::atoi(x.c_str() + 1));
            cur[dst] = x;
        }
        if (!W.backPred.empty()) pass(static_cast<int>(E) - 1, k);
    }
    // t, the places passed: their count less the sum of the d_p, no more than the window's w*E places.
    std::vector<std::string> sum = dead;
    while (sum.size() > 1) {
        std::vector<std::string> next;
        for (std::size_t i = 0; i + 1 < sum.size(); i += 2) { std::string n = c.fresh(sideOfAny(c, sum[i])); c.put("ADD", { sum[i], sum[i + 1], n }); next.push_back(n); }
        if (sum.size() % 2) next.push_back(sum.back());
        sum.swap(next);
    }
    std::string passed = c.fresh('A');
    if (dead.size() <= 15) c.put("SUB", { std::to_string(dead.size()), sum[0], passed });
    else { std::string n = c.fresh('A'); c.put("MVK", { std::to_string(dead.size()), n }); c.put("SUB", { n, sum[0], passed }); }
    // The window's bound, the least of the pointers' (8 - offset) * E, and t no more than it: t = b + min(t - b, 0).
    std::string bound;
    auto least = [&](const std::string &x, const std::string &y) {
        std::string d = c.fresh('A'), m = c.fresh('A'), n = c.fresh('A');
        c.put("SUB", { x, y, d }); c.put("SHR", { d, "31", m }); c.put("AND", { d, m, m }); c.put("ADD", { y, m, n });
        return n;
    };
    for (std::size_t p = 0; p < W.ptrs.size(); p++) {
        std::string w = c.fresh('A');
        c.put("SUB", { "8", O[p], w });
        if (E > 1) { std::string w2 = c.fresh('A'); c.put("ADD", { w, w, w2 }); w = w2; }
        bound = bound.empty() ? w : least(w, bound);
    }
    const std::string t = least(passed, bound);
    c.put("CMPEQ", { t, bound, Pc });
    std::string K = t;
    if (E > 1) { K = c.fresh('A'); c.put("SHR", { t, "1", K }); c.put("AND", { "1", t, Pj }); }
    // Each value left behind, made from K: the exit-0 one first, the exit-1 one under Pj, the head's under Pc.
    auto make = [&](const Aff &v, char s) {
        std::string x = c.fresh(s);
        if (v.base.empty()) { c.put("MVK", { std::to_string(v.c), x }); return x; }
        const long sb = step.count(v.base) ? step[v.base] : 0;
        std::string y = v.base;
        if (sb == 1) { std::string z = c.fresh(s); c.put("ADD", { K, v.base, z }); y = z; }
        else if (sb == -1) { std::string z = c.fresh(s); c.put("SUB", { v.base, K, z }); y = z; }
        if (v.c >= -16 && v.c <= 15) c.put("ADD", { std::to_string(v.c), y, x });
        else { std::string k2 = c.fresh(s); c.put("MVK", { std::to_string(v.c), k2 }); c.put("ADD", { y, k2, x }); }
        return x;
    };
    std::vector<std::pair<std::string, std::pair<std::string, std::string> > > moves;   // reg, value, predicate
    for (std::set<std::string>::const_iterator r = committed.begin(); r != committed.end(); ++r) {
        const char s = sideOf(*r);
        std::vector<std::string> xs(E);
        for (std::size_t j = 0; j < E; j++) if (W.exits[j].live & bitOf(*r)) xs[j] = make(at[j][*r], s);
        std::string head;
        bool same = true;                               // the head's value is the exits' own: one move does
        for (std::size_t j = 0; j < E; j++) if (!(W.exits[j].live & bitOf(*r)) || at[j][*r].base != *r || at[j][*r].c != 0) same = false;
        if ((W.liveHead & bitOf(*r)) && !same) { Aff h; h.base = *r; h.ok = true; head = make(h, s); }
        if (!xs[0].empty()) moves.push_back(std::make_pair(*r, std::make_pair(xs[0], std::string())));
        if (E > 1 && !xs[1].empty()) moves.push_back(std::make_pair(*r, std::make_pair(xs[1], xs[0].empty() ? std::string() : Pj)));
        if (!head.empty()) moves.push_back(std::make_pair(*r, std::make_pair(head, Pc)));
    }
    for (std::size_t m = 0; m < moves.size(); m++) c.put("MV", { moves[m].second.first, moves[m].first }, moves[m].second.second);
    std::vector<Line> code;
    if (!giveRegisters(c, pool, code, why)) return false;
    code.push_back(rebuilt("B", std::vector<std::string>(1, W.label), Pc));
    if (E == 1 || W.exits[0].target == W.exits[1].target) { if (!W.exits[0].target.empty()) code.push_back(rebuilt("B", std::vector<std::string>(1, W.exits[0].target))); }
    else {
        if (!W.exits[1].target.empty()) code.push_back(rebuilt("B", std::vector<std::string>(1, W.exits[1].target), Pj));
        else { code.push_back(rebuilt("B", std::vector<std::string>(1, W.exits[0].target.empty() ? W.label : W.exits[0].target), "!" + Pj)); }
        if (!W.exits[1].target.empty() && !W.exits[0].target.empty()) code.push_back(rebuilt("B", std::vector<std::string>(1, W.exits[0].target)));
    }
    if (tracing()) std::fprintf(stderr, "pipe %s: window, closed form, %zu pointers, %zu instructions for eight turns\n", W.label.c_str(), W.ptrs.size(), code.size());
    out.swap(code);
    return true;
}

bool emitWindow(const std::vector<Line> &v, Window &W, std::vector<Line> &out, std::string &why) {
    (void)v;
    // What the window may not take: every register live at the head or an exit, and the fixed ones.
    const std::uint64_t fixed = W.liveHead | W.liveOut | bitOf("A15") | bitOf("B15") | bitOf("B14") | bitOf("B3");
    std::set<std::string> written;
    for (std::size_t k = 0; k < W.body.size(); k++) {
        std::vector<std::string> reads, writes;
        readsAndWrites(W.body[k], reads, writes);
        for (std::size_t w = 0; w < writes.size(); w++) written.insert(writes[w]);
    }
    // The registers whose value the loop leaves behind: written in it and live at the head or an exit.
    std::set<std::string> committed;
    for (std::set<std::string>::const_iterator it = written.begin(); it != written.end(); ++it)
        if ((W.liveHead | W.liveOut) & bitOf(*it)) committed.insert(*it);
    std::vector<std::string> pool[2];
    for (int s = 0; s < 2; s++)
        for (int k = 0; k < 32; k++) {
            if (k >= 10 && k <= 15) continue;
            std::string r = std::string(s ? "B" : "A") + std::to_string(k);
            if (!(fixed & bitOf(r)) && !committed.count(r)) pool[s].push_back(r);
        }
    {
        std::vector<std::string> spare[2] = { pool[0], pool[1] };
        std::string cwhy;
        if (!std::getenv("CPP11_NOCLOSED") && emitClosed(W, committed, spare, out, cwhy)) return true;
        if (tracing() > 1) std::fprintf(stderr, "pipe %s: no closed form: %s\n", W.label.c_str(), cwhy.c_str());
    }
    // The exits by where they go; more than one place, and a selector says which was taken.
    std::vector<std::string> targets;
    for (std::size_t j = 0; j < W.exits.size(); j++) if (!has(targets, W.exits[j].target)) targets.push_back(W.exits[j].target);
    const bool multi = targets.size() > 1;
    bool needK = false, needZ = multi;
    for (std::size_t k = 0; k < W.body.size(); k++) {
        if (W.cls[k] == kCont) needK = true;
        if (W.cls[k] >= 0 && !isBranch(W.body[k].mnem)) needZ = true;
    }
    // The predicates: R, K and Z on one side, beside the arithmetic that makes them; Cr and the anchors' G anywhere.
    auto isFree = [&](int s, int k) { return std::find(pool[s].begin(), pool[s].end(), std::string(s ? "B" : "A") + std::to_string(k)) != pool[s].end(); };
    auto take = [&](int s, int k) { std::string r = std::string(s ? "B" : "A") + std::to_string(k); pool[s].erase(std::find(pool[s].begin(), pool[s].end(), r)); return r; };
    auto takePred = [&](int s) -> std::string { for (int k = 0; k < 3; k++) if (isFree(s, k)) return take(s, k); return ""; };
    auto preds = [&](int s) { int n = 0; for (int k = 0; k < 3; k++) if (isFree(s, k)) n++; return n; };
    const int cs = preds(0) >= preds(1) ? 0 : 1;
    const char ctl = cs ? 'B' : 'A';
    const std::string R = takePred(cs), Z = needZ ? takePred(cs) : "";
    std::vector<std::string> K;
    if (needK) { K.push_back(takePred(cs)); if (preds(cs) > 0 && preds(1 - cs) > 0) K.push_back(takePred(cs)); }
    std::string Cr = takePred(1 - cs); if (Cr.empty()) Cr = takePred(cs);
    std::vector<std::string> G(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond) { G[p] = takePred(1 - cs); if (G[p].empty()) G[p] = takePred(cs); }
    bool predsOk = !R.empty() && (!needZ || !Z.empty()) && (!needK || !K[0].empty()) && !Cr.empty();
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (W.ptrs[p].cond && G[p].empty()) predsOk = false;
    if (!predsOk) { why = "no condition registers for the turns"; return false; }
    std::string Sel;
    if (multi) { if (pool[cs].size() > 4) { Sel = pool[cs].back(); pool[cs].pop_back(); } else { why = "no register for the way out"; return false; } }
    WinCode c;
    std::map<std::string, std::string> cur;
    // Setup: each pointer's block and offset in it, and R = 0xFF >> (offset + span) for each, or-ed with one.
    std::vector<std::string> B(W.ptrs.size()), O(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++) {
        const Window::Ptr &P = W.ptrs[p];
        const char s = sideOf(P.reg);
        std::string u = P.reg;
        if (P.offmin != 0) { u = c.fresh(s); c.put("ADD", { std::to_string(P.offmin), P.reg, u }); }
        O[p] = c.fresh(s); c.put("AND", { "7", u, O[p] });
        B[p] = c.fresh(s); c.put("AND", { "-8", u, B[p] });
        std::string sh = O[p];
        if (P.offmax != P.offmin) { sh = c.fresh(s); c.put("ADD", { std::to_string(P.offmax - P.offmin), O[p], sh }); }
        std::string ff = c.fresh(s), m = c.fresh(s);
        c.put("MVK", { "255", ff });
        c.put("SHRU", { ff, sh, m });
        if (p == 0) c.put("MV", { m, R }); else c.put("AND", { R, m, R });
    }
    c.put("OR", { "1", R, R });                     // the first turn is always the loop's own
    // A committed register is read from a copy, its own name only ever written by the turns.
    for (std::set<std::string>::const_iterator it = committed.begin(); it != committed.end(); ++it) {
        std::string x = c.fresh(sideOf(*it));
        c.put("MV", { *it, x });
        cur[*it] = x;
    }
    // Which (n, mnem) a turn reaches, n the byte's place past the anchor's in the first turn.
    std::vector<std::vector<std::pair<long, std::string> > > reach(W.ptrs.size());
    for (std::size_t p = 0; p < W.ptrs.size(); p++)
        for (std::map<std::size_t, long>::const_iterator it = W.ptrs[p].loads.begin(); it != W.ptrs[p].loads.end(); ++it)
            reach[p].push_back(std::make_pair(it->second - W.ptrs[p].offmin, W.body[it->first].mnem));
    // The bytes of the block the later turns read, each where it wraps within the block, under the anchor's G.
    std::map<std::string, std::string> loaded;          // "p/n/mnem" -> register
    auto loadWrapped = [&](std::size_t p) {
        const char s = sideOf(W.ptrs[p].reg);
        for (int k = 1; k < 8; k++)
            for (std::size_t e = 0; e < reach[p].size(); e++) {
                const long n = reach[p][e].first + k;
                const std::string key = std::to_string(p) + "/" + std::to_string(n) + "/" + reach[p][e].second;
                bool atZero = false;
                for (std::size_t f = 0; f < reach[p].size(); f++) if (reach[p][f].first == n && reach[p][f].second == reach[p][e].second) atZero = true;
                if (atZero || loaded.count(key)) continue;
                std::string t = c.fresh(s), x = c.fresh(s);
                c.put("ADD", { std::to_string(n), O[p], t });
                c.put("AND", { "7", t, t });
                c.put(reach[p][e].second, { "*+" + B[p] + "[" + t + "]", x }, G[p]);
                loaded[key] = x;
            }
    };
    for (std::size_t p = 0; p < W.ptrs.size(); p++) if (!W.ptrs[p].cond) loadWrapped(p);
    auto commit = [&](const std::string &reg, const std::string &val, const std::string &pr) {
        if (committed.count(reg)) c.put("MV", { val, reg }, pr);
    };
    auto valueOf = [&](const std::string &r) { return cur.count(r) ? cur[r] : r; };
    // The test's mask: Y = -1 where the turn goes on and 0 where it leaves, from Q and the sense it goes on in.
    auto maskOf = [&](const std::string &q, bool goesOnWhenNonzero) {
        std::string y = c.fresh(ctl);
        if (c.isBool(q)) { if (goesOnWhenNonzero) c.put("NEG", { q, y }); else c.put("ADD", { "-1", q, y }); return y; }
        std::string t = c.fresh(ctl);
        c.put("CMPEQ", { "0", q, t });                  // 1 where Q is zero
        if (goesOnWhenNonzero) c.put("ADD", { "-1", t, y }); else c.put("NEG", { t, y });
        return y;
    };
    for (int k = 0; k < 8; k++) {
        bool exited = false;
        std::map<int, std::string> yOf;                 // exit -> its mask this turn
        int zFor = -1;                                  // the exit Z is made for, this turn
        bool kMade = false;
        const std::string Kk = needK ? K[static_cast<std::size_t>(k) % K.size()] : "";
        auto mask = [&](int j) {
            if (!yOf.count(j)) {
                const std::string &pr = W.exits[static_cast<std::size_t>(j)].pred;     // the predicate it leaves on
                const std::string q = pr[0] == '!' ? pr.substr(1) : pr;
                yOf[j] = maskOf(valueOf(q), pr[0] == '!');
            }
            return yOf[j];
        };
        auto leaving = [&](int j) {                     // Z: alive here and leaving by exit j
            if (zFor == j) return;
            std::string n = c.fresh(ctl);
            c.put("NOT", { mask(j), n });
            c.put("AND", { R, n, Z });
            zFor = j;
        };
        auto leaveBy = [&](int j) {                     // the exit itself: Cr, the selector, and R
            const std::string y = mask(j);
            if (multi) {
                leaving(j);
                const std::size_t id = static_cast<std::size_t>(std::find(targets.begin(), targets.end(), W.exits[static_cast<std::size_t>(j)].target) - targets.begin());
                c.put("MVK", { std::to_string(id), Sel }, Z);
            }
            c.put("MV", { y, Cr }, R);
            return y;
        };
        for (std::size_t i = 0; i < W.body.size(); i++) {
            const Line &l = W.body[i];
            const std::string pr = k == 0 && !exited ? "" : R;
            if (isBranch(l.mnem)) {             // an exit inside the turn: R loses the turns from here on where it leaves
                const std::string y = leaveBy(W.cls[i]);
                c.put("AND", { R, y, R });
                exited = true;
                continue;
            }
            const int cl = W.cls[i];
            std::string cpr = pr;
            if (cl == kCont) {
                if (!kMade) { c.put("AND", { R, mask(static_cast<int>(W.exits.size()) - 1), Kk }); kMade = true; }
                cpr = Kk;
            } else if (cl >= 0) { leaving(cl); cpr = Z; }
            const bool away = cl >= 0;          // done only on the way out: what stays on keeps the old value
            std::size_t ptr = W.ptrs.size();
            for (std::size_t p = 0; p < W.ptrs.size() && ptr == W.ptrs.size(); p++) if (W.ptrs[p].loads.count(i)) ptr = p;
            if (ptr < W.ptrs.size()) {
                const Window::Ptr &P = W.ptrs[ptr];
                std::string base; long off = 0; bool post = false;
                byteAddress(l.ops[0], base, off, post);
                const long n = P.loads.at(i) - P.offmin + k;
                const std::string key = std::to_string(ptr) + "/" + std::to_string(n) + "/" + l.mnem;
                if (k == 0 && P.cond && i == P.anchor) { c.put("MV", { R, G[ptr] }); loadWrapped(ptr); }
                if (!loaded.count(key)) {
                    if (k != 0) { why = "a byte no turn's block read was asked for"; return false; }
                    std::string x = c.fresh(sideOf(l.ops.back()));
                    c.put(l.mnem, { byteOperand(valueOf(base), post ? 0 : off), x }, pr);
                    loaded[key] = x;
                }
                const std::string &dst = l.ops.back();
                cur[dst] = loaded[key];
                c.affine.erase(dst);
                commit(dst, loaded[key], cpr);
                if (post) {
                    const std::pair<std::string, long> a = c.base(valueOf(base));
                    std::string nv = c.fresh(sideOf(base));
                    c.put("ADD", { std::to_string(a.second + 1), a.first, nv });
                    c.affine[nv] = std::make_pair(a.first, a.second + 1);
                    cur[base] = nv;
                    commit(base, nv, cpr);
                }
                continue;
            }
            const std::string &dst = l.ops.back();
            if (isLoad(l.mnem)) {               // read where it stands, under the turn's mask
                std::string x = c.fresh(sideOf(dst));
                c.put(l.mnem, { renamedBy(l.ops[0], cur), x }, cpr.empty() ? "" : cpr);
                if (!away) cur[dst] = x;
                commit(dst, x, cpr);
                continue;
            }
            if (l.mnem == "MV" && l.ops.size() == 2 && sideOf(l.ops[0])) {
                const std::string s = valueOf(l.ops[0]);
                if (sideOfAny(c, s) == sideOf(dst)) { if (!away) cur[dst] = s; commit(dst, s, cpr); continue; }
            }
            std::vector<std::string> ops;
            for (std::size_t o = 0; o + 1 < l.ops.size(); o++) ops.push_back(renamedBy(l.ops[o], cur));
            std::string x = c.fresh(sideOf(dst));
            // A constant added to a register that is itself another plus a constant: one add from that other.
            int ki = -1;
            if (l.mnem == "ADD" && ops.size() == 2) ki = isNumber(ops[0]) ? 0 : isNumber(ops[1]) ? 1 : -1;
            if (l.mnem == "SUB" && ops.size() == 2 && isNumber(ops[1])) ki = 1;
            if (ki >= 0 && !isNumber(ops[1 - ki])) {
                const long kc = std::atol(ops[static_cast<std::size_t>(ki)].c_str()) * (l.mnem == "SUB" ? -1 : 1);
                const std::pair<std::string, long> a = c.base(ops[static_cast<std::size_t>(1 - ki)]);
                if (a.second + kc >= -16 && a.second + kc <= 15 && sideOfAny(c, a.first) == sideOf(dst)) {
                    c.put("ADD", { std::to_string(a.second + kc), a.first, x });
                    c.affine[x] = std::make_pair(a.first, a.second + kc);
                } else c.put(l.mnem, { ops[0], ops[1], x });
            } else {
                ops.push_back(x);
                c.put(l.mnem, ops);
            }
            if (startsWith(l.mnem, "CMP")) c.boolean.insert(std::atoi(x.c_str() + 1));
            if (!away) { cur[dst] = x; }
            commit(dst, x, cpr);
        }
        // The turn's end: Y with bit k cleared, so that R keeps only the turns after this one in the block.
        if (!W.backPred.empty()) {
            const std::string y = leaveBy(static_cast<int>(W.exits.size()) - 1);
            std::string x = c.fresh(ctl);
            c.put("CLR", { y, std::to_string(k), std::to_string(k), x });
            c.put("AND", { R, x, R });
        } else {
            c.put("MVK", { "1", Cr }, R);
            c.put("CLR", { R, std::to_string(k), std::to_string(k), R });
        }
    }
    std::vector<Line> code;
    if (!giveRegisters(c, pool, code, why)) return false;
    // Round again while the last turn went on; else to where the turn that stopped was going.
    code.push_back(rebuilt("B", std::vector<std::string>(1, W.label), Cr));
    if (!multi) { if (!targets[0].empty()) code.push_back(rebuilt("B", std::vector<std::string>(1, targets[0]))); }
    else {
        bool falls = has(targets, "");
        std::vector<std::string> branches;
        for (std::size_t t = 0; t < targets.size(); t++) if (!targets[t].empty()) branches.push_back(targets[t]);
        for (std::size_t t = 0; t < branches.size(); t++) {
            const std::size_t id = static_cast<std::size_t>(std::find(targets.begin(), targets.end(), branches[t]) - targets.begin());
            if (!falls && t + 1 == branches.size()) { code.push_back(rebuilt("B", std::vector<std::string>(1, branches[t]))); break; }
            code.push_back(make("CMPEQ", std::to_string(id), Sel, R));
            code.push_back(rebuilt("B", std::vector<std::string>(1, branches[t]), R));
        }
    }
    if (tracing()) std::fprintf(stderr, "pipe %s: window, %zu pointers, %zu instructions for eight turns\n", W.label.c_str(), W.ptrs.size(), code.size());
    out.swap(code);
    return true;
}

}   // namespace

// Every candidate loop of the text, innermost first as the text is walked: rewritten in place where the pipelining pays.
void pipelineLoops(std::vector<Line> &v) {
    computeLiveness(v);
    if (tracing() > 5) for (std::size_t i = 0; i < v.size(); i++) std::fprintf(stderr, "| %s\n", v[i].raw.c_str());
    std::set<std::string> labels, named;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (isLabel(v[i])) labels.insert(labelName(v[i]));
        for (std::size_t o = 0; o < v[i].ops.size(); o++) named.insert(v[i].ops[o]);
    }
    std::vector<Line> out;
    std::size_t copied = 0;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (!v[i].instr || v[i].mnem != "B" || isCall(v[i], labels)) continue;
        if (v[i].pred.empty()) {                    // an unconditional branch is a back branch only to a label above
            bool above = false;
            for (std::size_t k = 0; k < i && !above; k++) above = isLabel(v[k]) && labelName(v[k]) == v[i].ops[0];
            if (!above) continue;
        }
        Loop L;
        std::string why;
        if (v[i].pred.empty() || !recognise(v, i, labels, named, L, why)) {
            if (v[i].pred.empty()) why = "an unconditional back branch";
            // Not counted: a loop that leaves on a byte it reads takes eight turns of one aligned block at a time.
            Window W;
            std::string wwhy;
            std::vector<Line> code;
            if (recogniseWindow(v, i, labels, named, W, wwhy) && W.head >= copied && emitWindow(v, W, code, wwhy)) {
                for (std::size_t k = copied; k <= W.head; k++) out.push_back(v[k]);
                for (std::size_t k = 0; k < code.size(); k++) out.push_back(code[k]);
                copied = W.back + 1;
                continue;
            }
            if (tracing()) std::fprintf(stderr, "pipe %s: %s [window: %s]\n", v[i].ops[0].c_str(), why.c_str(), wwhy.c_str());
            continue;
        }
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
