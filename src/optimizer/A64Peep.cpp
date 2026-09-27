#include "A64Peep.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <regex>
#include <sstream>
#include <vector>

namespace {

struct Line {
    std::string text;
    bool dead = false;
};

std::string trim(const std::string &s) {
    std::size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool isLabel(const std::string &t) { return !t.empty() && t.back() == ':'; }
bool isDirective(const std::string &t) { return !t.empty() && t[0] == '.'; }
std::string mnemonic(const std::string &t) { return t.substr(0, t.find(' ')); }

// A conditional branch, in either spelling the assembler takes: `b.eq` or `beq`.
bool isCondBranch(const std::string &m) {
    static const char *const cc[] = {"eq", "ne", "lt", "ge", "gt", "le", "lo", "hs", "hi", "ls",
                                     "mi", "pl", "vs", "vc", "cc", "cs", "al"};
    const std::string c = m.compare(0, 2, "b.") == 0 ? m.substr(2) : (m.size() == 3 && m[0] == 'b' ? m.substr(1) : "");
    for (const char *x : cc) if (c == x) return true;
    return false;
}
// A jump to a label: unconditional, conditional, or on a register's value.
bool isJump(const std::string &m) {
    return m == "b" || isCondBranch(m) || m == "cbz" || m == "cbnz" || m == "tbz" || m == "tbnz";
}
// A jump, a call or a return: nothing is carried across one in a register the callee may use.
bool isBranch(const std::string &m) { return isJump(m) || m == "bl" || m == "blr" || m == "br" || m == "ret"; }

const std::regex kPush(R"(^str ([xd])(\d+), \[sp, #-16\]!$)");
const std::regex kPop(R"(^ldr ([xd])(\d+), \[sp\], #16$)");
const std::regex kSp(R"(\bsp\b)");

// **A push its pop follows in the same straight run keeps its value in a register**: x12-x15 for a general
// value and d16-d23 for a double, by depth - caller-saved, and named nowhere else in the backend's code.
void pushesToRegisters(std::vector<Line> &ls) {
    struct Open { int at; char cls; bool ok; };
    std::vector<Open> open;
    auto poison = [&]() { for (Open &o : open) o.ok = false; };
    for (int k = 0; k < static_cast<int>(ls.size()); ++k) {
        if (ls[k].dead) continue;
        const std::string t = trim(ls[k].text);
        if (t.empty() || isDirective(t)) continue;
        if (isLabel(t)) { poison(); continue; }
        std::smatch m;
        if (std::regex_match(t, m, kPush)) {
            open.push_back(Open{k, m[1].str()[0], true});
            continue;
        }
        if (std::regex_match(t, m, kPop)) {
            if (open.empty()) continue;
            const Open o = open.back();
            open.pop_back();
            const int depth = static_cast<int>(open.size());
            const char cls = m[1].str()[0];
            if (!o.ok || o.cls != cls || (cls == 'x' ? depth >= 4 : depth >= 8)) continue;
            std::smatch pm;
            const std::string pt = trim(ls[o.at].text);
            std::regex_match(pt, pm, kPush);
            const std::string hold = cls == 'x' ? "x" + std::to_string(12 + depth) : "d" + std::to_string(16 + depth);
            const std::string op = cls == 'x' ? "mov" : "fmov";
            ls[o.at].text = "  " + op + " " + hold + ", " + pm[1].str() + pm[2].str();
            ls[k].text = "  " + op + " " + m[1].str() + m[2].str() + ", " + hold;
            continue;
        }
        if (isBranch(mnemonic(t)) || std::regex_search(t, kSp)) poison();
    }
}

const std::regex kMovK(R"(^mov x9, #(\d+)$)");
const std::regex kSubFp(R"(^sub x(\d+), x29, x9$)");
const std::regex kLoadAt(R"(^(ldr|ldrsw|ldrb|ldrsb|ldrh|ldrsh) ([wx])(\d+), \[x(\d+)\]$)");
const std::regex kX9(R"(\b[wx]9\b)");

// Whether x9 is written before anything reads it, in the straight run after line k.
bool x9DeadAfter(const std::vector<Line> &ls, int k) {
    for (int j = k + 1; j < static_cast<int>(ls.size()); ++j) {
        if (ls[j].dead) continue;
        const std::string t = trim(ls[j].text);
        if (t.empty() || isDirective(t)) continue;
        if (isLabel(t) || isBranch(mnemonic(t))) return false;
        if (!std::regex_search(t, kX9)) continue;
        return std::regex_match(t, kMovK) || t.compare(0, 8, "mov x9, ") == 0;
    }
    return false;
}

// **A local read through its address, the address dead after**: `mov x9, #k; sub xA, x29, x9; ldr xA, [xA]`
// is one `ldur xA, [x29, #-k]`, and the constant goes too where x9 is written again before it is read.
void directLocalLoads(std::vector<Line> &ls) {
    std::vector<int> idx;
    for (int k = 0; k < static_cast<int>(ls.size()); ++k) {
        const std::string t = trim(ls[k].text);
        if (!t.empty() && !isDirective(t)) idx.push_back(k);
    }
    for (std::size_t n = 0; n + 2 < idx.size(); ++n) {
        const int a = idx[n], b = idx[n + 1], c = idx[n + 2];
        if (ls[a].dead || ls[b].dead || ls[c].dead) continue;
        std::smatch ma, mb, mc;
        const std::string ta = trim(ls[a].text), tb = trim(ls[b].text), tc = trim(ls[c].text);
        if (!std::regex_match(ta, ma, kMovK) || !std::regex_match(tb, mb, kSubFp) || !std::regex_match(tc, mc, kLoadAt))
            continue;
        const long k = std::strtol(ma[1].str().c_str(), nullptr, 10);
        if (k <= 0 || k > 256 || mb[1] != mc[4] || mc[3] != mb[1]) continue;
        const std::string op = mc[1].str();
        ls[b].dead = true;
        ls[c].text = "  ld" + std::string("u") + op.substr(2) + " " + mc[2].str() + mc[3].str() + ", [x29, #-" + std::to_string(k) + "]";
        if (x9DeadAfter(ls, c)) ls[a].dead = true;
    }
}

// ----- a register model: what each instruction reads and writes, and whether a value is dead after a line

// A register as the model sees it: g for x/w, f for d/s, and its number; -1 for anything else.
struct R { char cls = 0; int n = -1; bool ok() const { return n >= 0; } bool operator==(const R &o) const { return cls == o.cls && n == o.n; } };

R regOf(const std::string &tok) {
    if (tok.size() < 2 || tok.size() > 3) return R{};
    const char c = tok[0];
    for (std::size_t i = 1; i < tok.size(); ++i) if (tok[i] < '0' || tok[i] > '9') return R{};
    const int n = std::atoi(tok.c_str() + 1);
    if ((c == 'x' || c == 'w') && n <= 30) return R{'g', n};
    if ((c == 'd' || c == 's') && n <= 31) return R{'f', n};
    return R{};
}

struct Ins {
    std::string m;
    std::vector<std::string> ops;     // split at the top-level commas
};

Ins parseIns(const std::string &t) {
    Ins i;
    const std::size_t sp = t.find(' ');
    i.m = t.substr(0, sp);
    if (sp == std::string::npos) return i;
    std::string cur;
    int depth = 0;
    for (std::size_t k = sp + 1; k < t.size(); ++k) {
        const char c = t[k];
        if (c == '[') ++depth;
        if (c == ']') --depth;
        if (c == ',' && depth == 0) { i.ops.push_back(trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    if (!trim(cur).empty()) i.ops.push_back(trim(cur));
    return i;
}

// The registers named inside an operand, a memory one's base and index included.
std::vector<R> regsIn(const std::string &op) {
    std::vector<R> out;
    std::string tok;
    for (std::size_t k = 0; k <= op.size(); ++k) {
        const char c = k < op.size() ? op[k] : ' ';
        if (std::isalnum(static_cast<unsigned char>(c))) { tok += c; continue; }
        const R r = regOf(tok);
        if (r.ok()) out.push_back(r);
        tok.clear();
    }
    return out;
}

bool noDest(const std::string &m) {
    return m.compare(0, 2, "st") == 0 || m == "cmp" || m == "cmn" || m == "tst" || m == "fcmp" || m == "fcmpe" ||
           m == "cbz" || m == "cbnz" || m == "tbz" || m == "tbnz" || isBranch(m) || m == "ccmp";
}
bool flagReader(const std::string &m) {
    return isCondBranch(m) || m == "cset" || m == "csetm" || m == "csel" || m == "csinc" || m == "csinv" ||
           m == "csneg" || m == "cinc" || m == "cneg" || m == "fcsel" || m == "adc" || m == "sbc" || m == "ccmp";
}
bool flagWriter(const std::string &m) {
    return m == "cmp" || m == "cmn" || m == "tst" || m == "adds" || m == "subs" || m == "ands" || m == "fcmp" ||
           m == "fcmpe";
}

struct Effect { std::vector<R> reads, writes; bool known = true; };

// What an instruction reads and writes, from its operands; a writeback base is both.
Effect effectOf(const Ins &i) {
    Effect e;
    const bool nd = noDest(i.m);
    const bool pair = i.m == "ldp" || i.m == "ldnp" || i.m == "ldpsw";
    const bool keeps = i.m == "movk" || i.m == "bfi" || i.m == "bfxil" || i.m == "bfm";
    for (std::size_t k = 0; k < i.ops.size(); ++k) {
        const std::string &op = i.ops[k];
        const bool mem = !op.empty() && op[0] == '[';
        const bool dest = !nd && !mem && (k == 0 || (pair && k == 1));
        for (const R &r : regsIn(op)) {
            if (dest) { e.writes.push_back(r); if (keeps) e.reads.push_back(r); }
            else e.reads.push_back(r);
        }
        const bool writeback = mem && (op.back() == '!' || k + 1 < i.ops.size());
        if (writeback) for (const R &r : regsIn(op)) { e.writes.push_back(r); break; }
    }
    return e;
}

bool has(const std::vector<R> &v, const R &r) { for (const R &x : v) if (x == r) return true; return false; }

// The argument and result registers of the calling convention, and what a call leaves changed.
bool argOrResult(const R &r) { return r.n <= 8; }
// Whether the function's result travels in x0-x7: not for void or a floating result. Per compile thread.
thread_local bool tResultInX = true;
bool resultLive(const R &r) { return r.cls == 'f' ? r.n <= 7 : (tResultInX && r.n <= 7); }
bool callerSaved(const R &r) { return r.cls == 'g' ? r.n <= 18 : (r.n <= 7 || r.n >= 16); }

class Body {
public:
    explicit Body(std::vector<Line> &ls) : ls_(ls) {
        for (int k = 0; k < static_cast<int>(ls_.size()); ++k) {
            const std::string t = trim(ls_[k].text);
            if (isLabel(t)) labels_[t.substr(0, t.size() - 1)] = k;
        }
    }
    std::string text(int k) const { return trim(ls_[k].text); }
    bool isIns(int k) const {
        if (ls_[k].dead) return false;
        const std::string t = text(k);
        return !t.empty() && !isDirective(t) && !isLabel(t);
    }
    // The next live instruction after k in the same straight run; -1 at a label or the end.
    int next(int k) const {
        for (int j = k + 1; j < static_cast<int>(ls_.size()); ++j) {
            if (ls_[j].dead) continue;
            const std::string t = text(j);
            if (t.empty() || isDirective(t)) continue;
            return isLabel(t) ? -1 : j;
        }
        return -1;
    }
    int prev(int k) const {
        for (int j = k - 1; j >= 0; --j) {
            if (ls_[j].dead) continue;
            const std::string t = text(j);
            if (t.empty() || isDirective(t)) continue;
            return isLabel(t) ? -1 : j;
        }
        return -1;
    }
    // **Whether r's value after line k is read on no path**, following jumps through the labels of the
    // body. A return reads the result registers; a label outside the body, or anything unsure, is a read.
    bool deadAfter(const R &r, int k) const {
        std::vector<int> work{k + 1};
        // Line k itself may be the jump: after it, r goes where the jump goes as well as, or instead of, on.
        if (isIns(k)) {
            const Ins i = parseIns(text(k));
            if (isJump(i.m)) {
                const auto l = labels_.find(i.ops.empty() ? std::string() : i.ops.back());
                if (l == labels_.end()) return false;
                if (i.m == "b") work.clear();
                work.push_back(l->second);
            } else if (i.m == "br" || i.m == "ret") {
                return false;
            }
        }
        std::vector<bool> seen(ls_.size() + 1, false);
        int budget = 400;
        while (!work.empty()) {
            int j = work.back();
            work.pop_back();
            for (;; ++j) {
                if (--budget < 0) return false;
                if (j >= static_cast<int>(ls_.size())) { if (resultLive(r)) return false; break; }
                if (seen[j]) break;
                seen[j] = true;
                if (ls_[j].dead) continue;
                const std::string t = text(j);
                if (t.empty() || isDirective(t) || isLabel(t)) continue;
                const Ins i = parseIns(t);
                const Effect e = effectOf(i);
                if (has(e.reads, r)) return false;
                // A call may also leave by an exception to a landing pad, which reads what it finds in the
                // callee-saved registers: those are live at every call.
                if (i.m == "bl" || i.m == "blr") {
                    if (argOrResult(r) && !(r.cls == 'g' && r.n == 8)) return false;
                    if (callerSaved(r)) break;
                    return false;
                }
                if (i.m == "ret") { if (resultLive(r)) return false; break; }
                if (i.m == "br") return false;
                if (has(e.writes, r)) break;
                if (!isJump(i.m)) continue;
                const std::string target = i.ops.empty() ? std::string() : i.ops.back();
                const auto l = labels_.find(target);
                if (l == labels_.end()) {
                    if (target.compare(0, 9, "L.return.") != 0 || resultLive(r)) return false;
                } else {
                    work.push_back(l->second);
                }
                if (i.m == "b") break;
            }
        }
        return true;
    }
    // Whether the flags are written before they are read on both paths after line k, in straight runs.
    bool flagsDeadAfter(int k, const std::string &target) const {
        auto straight = [&](int j) {
            for (; j < static_cast<int>(ls_.size()); ++j) {
                if (ls_[j].dead) continue;
                const std::string t = text(j);
                if (t.empty() || isDirective(t) || isLabel(t)) continue;
                const std::string m = mnemonic(t);
                if (flagReader(m)) return false;
                if (flagWriter(m)) return true;
                if (isBranch(m) || m == "cbz" || m == "cbnz") return m == "bl" || m == "blr";
            }
            return false;
        };
        const auto l = labels_.find(target);
        return l != labels_.end() && straight(k + 1) && straight(l->second + 1);
    }

private:
    std::vector<Line> &ls_;
    std::map<std::string, int> labels_;
};

// The width asTok names a register in, for register `to`.
std::string spell(const std::string &asTok, const R &to) {
    return std::string(1, asTok[0]) + std::to_string(to.n);
}

// Replace every read of `from` in instruction i's source operands (and memory operands) by `to`.
std::string substituteReads(const Ins &i, const R &from, const R &to) {
    const bool nd = noDest(i.m);
    std::string out = i.m;
    for (std::size_t k = 0; k < i.ops.size(); ++k) {
        std::string op = i.ops[k];
        const bool mem = !op.empty() && op[0] == '[';
        const bool dest = !nd && !mem && k == 0;
        if (!dest) {
            std::string res, tok;
            for (std::size_t c = 0; c <= op.size(); ++c) {
                const char ch = c < op.size() ? op[c] : '\0';
                if (std::isalnum(static_cast<unsigned char>(ch))) { tok += ch; continue; }
                const R r = regOf(tok);
                res += (r.ok() && r == from) ? spell(tok, to) : tok;
                tok.clear();
                if (ch) res += ch;
            }
            op = res;
        }
        out += (k == 0 ? " " : ", ") + op;
    }
    return out;
}

bool scratch(const R &r) {
    if (r.cls == 'g') return r.n <= 15 && r.n != 8;
    return r.n <= 7 || r.n >= 16;
}

const std::regex kMovReg(R"(^(mov|fmov) ([xwds])(\d+), ([xwds])(\d+)$)");
const std::regex kMovAny(R"(^(mov|fmov) ([xwds])(\d+), (?:#-?\d+|[xwds]\d+|[xw]zr)$)");

// A register a rule may rename or drop a copy into: not the frame, the link, the platform or sret register.
bool allowed(const R &r) { return r.cls == 'f' || (r.n <= 28 && r.n != 8 && (r.n < 16 || r.n > 18)); }

// The width letters an instruction names register r in.
std::string lettersOf(const Ins &i, const R &r) {
    std::string out;
    for (const std::string &op : i.ops) {
        std::string tok;
        for (std::size_t c = 0; c <= op.size(); ++c) {
            const char ch = c < op.size() ? op[c] : '\0';
            if (std::isalnum(static_cast<unsigned char>(ch))) { tok += ch; continue; }
            if (regOf(tok).ok() && regOf(tok) == r) out += tok[0];
            tok.clear();
        }
    }
    return out;
}
const std::regex kMovImm(R"(^mov ([xw])(\d+), #(-?\d+)$)");
const std::regex kSubX9(R"(^sub ([xw])(\d+), x29, x9$)");
const std::regex kMemAt(R"(^(ldr|ldrsw|ldrb|ldrsb|ldrh|ldrsh|str|strb|strh) ([wxds])(\d+), \[x(\d+)\]$)");
const std::regex kCset(R"(^cset ([xw])(\d+), (\w+)$)");
const std::regex kCmpZero(R"(^cmp ([xw])(\d+), #0$)");

std::string invert(const std::string &c) {
    static const char *pairs[][2] = {{"eq", "ne"}, {"lt", "ge"}, {"gt", "le"}, {"lo", "hs"}, {"hi", "ls"},
                                     {"mi", "pl"}, {"vs", "vc"}, {"cc", "cs"}};
    for (auto &p : pairs) {
        if (c == p[0]) return p[1];
        if (c == p[1]) return p[0];
    }
    return std::string();
}

// One sweep of the rules; whether any fired.
bool sweep(std::vector<Line> &ls) {
    Body b(ls);
    bool changed = false;
    for (int k = 0; k < static_cast<int>(ls.size()); ++k) {
        if (!b.isIns(k)) continue;
        const std::string t = b.text(k);
        std::smatch m;
        // A frame address through x9, the address read once: `mov x9, #k; sub xA, x29, x9; op v, [xA]`.
        if (std::regex_match(t, m, kSubX9)) {
            const int p = b.prev(k), u = b.next(k);
            std::smatch pm, um;
            const std::string pt = p >= 0 ? b.text(p) : std::string(), ut = u >= 0 ? b.text(u) : std::string();
            if (p >= 0 && u >= 0 && std::regex_match(pt, pm, kMovK) && std::regex_match(ut, um, kMemAt)) {
                const long off = std::strtol(pm[1].str().c_str(), nullptr, 10);
                const R base{'g', std::atoi(m[2].str().c_str())};
                const bool load = um[1].str()[0] == 'l';
                const R val = regOf(um[2].str() + um[3].str());
                if (std::atoi(um[4].str().c_str()) == base.n && off > 0 && off <= 256 &&
                    ((load && val == base) || (!(val == base) && b.deadAfter(base, u)))) {
                    const std::string op = um[1].str();
                    ls[u].text = "  " + op.substr(0, 2) + "u" + op.substr(2) + " " + um[2].str() + um[3].str() +
                                 ", [x29, #-" + std::to_string(off) + "]";
                    ls[k].dead = true;
                    if (b.deadAfter(R{'g', 9}, u)) ls[p].dead = true;
                    changed = true;
                    continue;
                }
            }
            // Or computed once, the constant folded: `sub xA, x29, #k` - x9 dead after, or the address itself.
            if (p >= 0 && std::regex_match(pt, pm, kMovK) && (m[2] == "9" || b.deadAfter(R{'g', 9}, k))) {
                const long off = std::strtol(pm[1].str().c_str(), nullptr, 10);
                if (off >= 0 && off < 4096) {
                    ls[k].text = "  sub " + m[1].str() + m[2].str() + ", x29, #" + std::to_string(off);
                    ls[p].dead = true;
                    changed = true;
                    continue;
                }
            }
        }
        // A test of a flag already tested: `cset a, c; cmp a, #0; cset a, ne` where the flags are set again next.
        if (std::regex_match(t, m, kCset)) {
            const int c1 = b.next(k), c2 = c1 >= 0 ? b.next(c1) : -1;
            std::smatch cm, sm;
            const std::string c1t = c1 >= 0 ? b.text(c1) : std::string();
            const std::string c2t = c2 >= 0 ? b.text(c2) : std::string();
            if (c1 >= 0 && std::regex_match(c1t, cm, kCmpZero) && cm[2] == m[2] && c2 >= 0) {
                const int after = b.next(c2);
                if (std::regex_match(c2t, sm, kCset) && sm[2] == m[2] && sm[3] == "ne" && after >= 0 &&
                    flagWriter(mnemonic(b.text(after)))) {
                    ls[c1].dead = ls[c2].dead = true;
                    changed = true;
                    continue;
                }
                // `cset a, c; cmp a, #0; beq L` is `b.!c L` where a and the flags are dead after.
                const Ins br = parseIns(c2t);
                if ((br.m == "b.eq" || br.m == "b.ne" || br.m == "beq" || br.m == "bne") && br.ops.size() == 1) {
                    const R a{'g', std::atoi(m[2].str().c_str())};
                    const bool onZero = br.m == "b.eq" || br.m == "beq";
                    const std::string cc = onZero ? invert(m[3].str()) : m[3].str();
                    if (!cc.empty() && b.deadAfter(a, c2) && b.flagsDeadAfter(c2, br.ops[0])) {
                        ls[k].dead = ls[c1].dead = true;
                        ls[c2].text = "  b." + cc + " " + br.ops[0];
                        changed = true;
                        continue;
                    }
                }
            }
        }
        if (std::regex_match(t, m, kMovReg)) {
            const R d = regOf(m[2].str() + m[3].str()), s = regOf(m[4].str() + m[5].str());
            const char L = m[2].str()[0];
            const bool narrow = L == 'w' || L == 's';     // a narrow copy zeroes the upper half of d
            if (m[2] != m[4]) continue;
            if (d.ok() && d == s && !narrow) { ls[k].dead = true; changed = true; continue; }   // mov x0, x0
            if (!d.ok() || !s.ok() || d == s || !(d.cls == s.cls) || !allowed(d) || !allowed(s)) continue;
            // Retarget: the instruction before made s for this copy alone.
            const int p = b.prev(k);
            if (p >= 0 && b.deadAfter(s, k)) {
                const Ins pi = parseIns(b.text(p));
                const Effect pe = effectOf(pi);
                const bool simple = !noDest(pi.m) && pi.ops.size() >= 2 && pe.writes.size() == 1 && pe.writes[0] == s &&
                                    pi.m != "movk" && pi.m != "bfi" && pi.m != "bfxil" && pi.m != "ldp" && pi.m != "ldpsw" &&
                                    pi.ops[0][0] != '[' && regOf(pi.ops[0]).ok();
                if (simple && (!narrow || pi.ops[0][0] == L)) {
                    std::string rest = b.text(p).substr(pi.m.size() + 1 + pi.ops[0].size());
                    ls[p].text = "  " + pi.m + " " + spell(pi.ops[0], d) + rest;
                    ls[k].dead = true;
                    changed = true;
                    continue;
                }
            }
            // Forward: the one reader of d takes s, d dead after it and neither changed between.
            for (int u = b.next(k); u >= 0; u = b.next(u)) {
                const Ins ui = parseIns(b.text(u));
                const Effect ue = effectOf(ui);
                if (isBranch(ui.m)) break;
                if (has(ue.reads, d)) {
                    const bool dFree = has(ue.writes, d) || b.deadAfter(d, u);
                    const std::string ls_ = lettersOf(ui, d);
                    const bool widthOk = !narrow || ls_.find_first_not_of(L) == std::string::npos;
                    if (dFree && widthOk) {
                        ls[u].text = "  " + substituteReads(ui, d, s);
                        ls[k].dead = true;
                        changed = true;
                    }
                    break;
                }
                if (has(ue.writes, d) || has(ue.writes, s)) break;
            }
            if (ls[k].dead) continue;
        }
        // An immediate copied on: `mov a, #n; mov b, a` is `mov b, #n` where a is dead after.
        if (std::regex_match(t, m, kMovImm)) {
            const int u = b.next(k);
            std::smatch um;
            const std::string ut = u >= 0 ? b.text(u) : std::string();
            const R a{'g', std::atoi(m[2].str().c_str())};
            if (u >= 0 && std::regex_match(ut, um, kMovReg) && um[1] == "mov" && um[2] == "x" && um[4] == "x" &&
                regOf(um[4].str() + um[5].str()) == a &&
                scratch(a) && m[1] == "x" && b.deadAfter(a, u)) {
                ls[u].text = "  mov " + um[2].str() + um[3].str() + ", #" + m[3].str();
                ls[k].dead = true;
                changed = true;
                continue;
            }
        }
    }
    // A copy nothing reads is gone: `mov x9, #36` whose x9 is written again first.
    for (int k = 0; k < static_cast<int>(ls.size()); ++k) {
        if (!b.isIns(k)) continue;
        std::smatch m;
        const std::string t = b.text(k);
        if (!std::regex_match(t, m, kMovAny)) continue;
        const R d = regOf(m[2].str() + m[3].str());
        if (d.ok() && allowed(d) && b.deadAfter(d, k)) { ls[k].dead = true; changed = true; }
    }
    return changed;
}

const std::regex kAddrImm(R"(^(?:sub|add) [xw]\d+, x29, #(\d+)$)");
const std::regex kBaseAt(R"(^(ldr|ldrsw|ldrb|ldrsb|ldrh|ldrsh|str|strb|strh) ([wxds](?:\d+|zr)), \[x(\d+)\]$)");

// **A frame address used only as the plain base of loads and stores**, in one straight run and dead after
// the last: each access takes `[x29, #-k]` itself and the address goes - `i++` through `sub x0, x29, #k`.
bool foldFrameAddresses(std::vector<Line> &ls) {
    Body b(ls);
    bool changed = false;
    for (int k = 0; k < static_cast<int>(ls.size()); ++k) {
        if (!b.isIns(k)) continue;
        std::smatch m;
        const std::string t = b.text(k);
        if (!std::regex_match(t, m, kAddrImm) || t.compare(0, 3, "sub") != 0) continue;
        const long off = std::strtol(m[1].str().c_str(), nullptr, 10);
        const R a = regOf(t.substr(4, t.find(',') - 4));
        if (!a.ok() || a.cls != 'g' || off <= 0 || off > 256 || !scratch(a)) continue;
        std::vector<int> uses;
        int last = -1;
        bool fits = true;
        for (int u = b.next(k); u >= 0; u = b.next(u)) {
            const Ins ui = parseIns(b.text(u));
            const Effect ue = effectOf(ui);
            const bool reads = has(ue.reads, a), writes = has(ue.writes, a);
            if (!reads && !writes) { if (isBranch(ui.m)) break; continue; }
            std::smatch um;
            const std::string ut = b.text(u);
            if (!reads || !std::regex_match(ut, um, kBaseAt) || std::atoi(um[3].str().c_str()) != a.n ||
                regOf(um[2].str()) == a) { fits = fits && !reads; break; }
            uses.push_back(u);
            last = u;
            if (writes) break;
        }
        if (!fits || uses.empty() || !b.deadAfter(a, last)) continue;
        for (int u : uses) {
            std::smatch um;
            const std::string ut = b.text(u);
            std::regex_match(ut, um, kBaseAt);
            const std::string op = um[1].str();
            ls[u].text = "  " + op.substr(0, 2) + "u" + op.substr(2) + " " + um[2].str() + ", [x29, #-" + std::to_string(off) + "]";
        }
        ls[k].dead = true;
        changed = true;
    }
    return changed;
}

const std::regex kSlot(R"(^(ldur|ldursw|ldurb|ldursb|ldurh|ldursh|stur|sturb|sturh) ([wxds])(\d+|zr), \[x29, #-(\d+)\]$)");

// **A local that is only ever read and written whole, its address never formed**, held in a callee-saved
// register for the whole function: x19-x28 for an integer, d8-d15 for a double. `saved` names those taken.
void promoteLocals(std::vector<Line> &ls, std::vector<std::string> &saved) {
    struct Slot { int size = 0; char cls = 0; int uses = 0; bool ok = true; };
    std::map<long, Slot> slots;
    long formedFrom = -1;                 // an address formed at x29-k reaches every slot at or above it
    Body b(ls);
    for (int k = 0; k < static_cast<int>(ls.size()); ++k) {
        if (!b.isIns(k)) continue;
        const std::string t = b.text(k);
        if (t.find("setjmp") != std::string::npos) return;
        if (t.find("x29") == std::string::npos) continue;
        std::smatch m;
        if (std::regex_match(t, m, kSlot)) {
            const std::string op = m[1].str();
            const char w = m[2].str()[0];
            const int size = op.back() == 'b' ? 1 : op.back() == 'h' ? 2 : op == "ldursw" ? 4 : (w == 'x' || w == 'd') ? 8 : 4;
            Slot &sl = slots[std::strtol(m[4].str().c_str(), nullptr, 10)];
            const char cls = (w == 'd' || w == 's') ? 'f' : 'g';
            if (sl.uses != 0 && (sl.size != size || sl.cls != cls)) sl.ok = false;
            sl.size = size;
            sl.cls = cls;
            ++sl.uses;
            continue;
        }
        if (std::regex_match(t, m, kAddrImm)) {
            if (t.compare(0, 3, "sub") == 0) formedFrom = std::max(formedFrom, std::strtol(m[1].str().c_str(), nullptr, 10));
            continue;
        }
        if (t == "sub x9, x29, x9" || std::regex_match(t, kSubX9)) {
            const int p = b.prev(k);
            std::smatch pm;
            const std::string pt = p >= 0 ? b.text(p) : std::string();
            if (p < 0 || !std::regex_match(pt, pm, kMovK)) return;
            formedFrom = std::max(formedFrom, std::strtol(pm[1].str().c_str(), nullptr, 10));
            continue;
        }
        return;                                // any other use of the frame pointer: take nothing
    }
    for (auto &a : slots)
        for (auto &c : slots)
            if (&a != &c && a.first - a.second.size < c.first && c.first - c.second.size < a.first) a.second.ok = false;
    std::vector<std::pair<int, long>> order;
    for (auto &sl : slots)
        if (sl.second.ok && sl.first > formedFrom && (sl.second.cls == 'g' || sl.second.size == 8 || sl.second.size == 4))
            order.push_back({-sl.second.uses, sl.first});
    std::sort(order.begin(), order.end());
    std::map<long, std::string> reg;           // slot -> register number, spelled without its width letter
    int nextG = 19, nextF = 8;
    for (const auto &o : order) {
        const Slot &sl = slots[o.second];
        if (sl.cls == 'g' && nextG <= 28) { reg[o.second] = std::to_string(nextG); saved.push_back("x" + std::to_string(nextG++)); }
        if (sl.cls == 'f' && nextF <= 15) { reg[o.second] = std::to_string(nextF); saved.push_back("d" + std::to_string(nextF++)); }
    }
    for (Line &l : ls) {
        if (l.dead) continue;
        const std::string t = trim(l.text);
        std::smatch m;
        if (!std::regex_match(t, m, kSlot)) continue;
        const auto r = reg.find(std::strtol(m[4].str().c_str(), nullptr, 10));
        if (r == reg.end()) continue;
        const std::string op = m[1].str(), v = m[2].str() + m[3].str(), n = r->second;
        const Slot &sl = slots[r->first];
        std::string out;
        if (sl.cls == 'f') {
            const std::string R = (sl.size == 8 ? "d" : "s") + n;
            out = op[0] == 'l' ? "fmov " + v + ", " + R : "fmov " + R + ", " + v;
        } else if (op[0] == 's') {
            out = sl.size == 8 ? "mov x" + n + ", " + v : "mov w" + n + ", " + v;
        } else if (op == "ldur") {
            out = "mov " + v + ", " + (m[2] == "x" ? "x" : "w") + n;
        } else if (op == "ldursw") {
            out = "sxtw " + v + ", w" + n;
        } else {
            const std::string ext = std::string(op.size() == 6 ? "s" : "u") + "xt" + op.back();
            out = ext + " " + v + ", w" + n;
        }
        l.text = "  " + out;
    }
}

}

std::string a64Peephole(const std::string &text, int level, std::vector<std::string> *saved, bool resultInX) {
    if (level <= 0) return text;
    tResultInX = resultInX;
    std::vector<Line> ls;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) ls.push_back(Line{l});
    directLocalLoads(ls);
    pushesToRegisters(ls);
    for (int round = 0; round < 8; ++round) {
        const bool swept = sweep(ls);
        if (!foldFrameAddresses(ls) && !swept) break;
    }
    if (saved != nullptr) {
        promoteLocals(ls, *saved);
        if (!saved->empty()) for (int round = 0; round < 8 && sweep(ls); ++round) {}
    }
    std::string out;
    out.reserve(text.size());
    for (const Line &l : ls)
        if (!l.dead) { out += l.text; out += '\n'; }
    return out;
}
