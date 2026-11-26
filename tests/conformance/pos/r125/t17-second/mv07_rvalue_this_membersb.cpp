// Members of rvalues are xvalues ([expr.ref]/4 as amended by DR 421): std::move(pair).first binds T&&; member access on a prvalue function result binds T&&; overload picked accordingly.
#include <cstdio>
#include <utility>
#include <string>
struct S { std::string s; int i; S(const char *c, int i) : s(c), i(i) {} };
S make() { return S("made", 1); }
const char *take(std::string &&) { return "&&"; }
const char *take(const std::string &) { return "const&"; }
int main() {
    S s("local", 2);
    std::printf("%s %s %s\n", take(s.s), take(std::move(s).s), take(make().s));
    std::string moved = std::move(s).s; std::printf("%s %d\n", moved.c_str(), s.s.size() == 0 || s.s.size() > 0);
    std::printf("%s\n", take(std::move(s.s)));
    return 0;
}
