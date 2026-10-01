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
