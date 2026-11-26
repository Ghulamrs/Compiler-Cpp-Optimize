// [conv.bool]: a pointer to member converts to bool implicitly, its null false and anything else true - a data
// member at offset 0 included, whose null is not the number 0.
#include <cstdio>
struct S { int m; int n; int f() { return 1; } virtual int v() { return 2; } };
static const char *which(bool b) { return b ? "true" : "false"; }
int main() {
    int S::*first = &S::m;
    int S::*second = &S::n;
    int S::*none = nullptr;
    int (S::*fn)() = &S::f;
    int (S::*vf)() = &S::v;
    int (S::*noFn)() = nullptr;
    bool a = first;
    bool b = second;
    bool c = none;
    bool d = fn;
    bool e = vf;
    bool f = noFn;
    bool g(first);
    const bool h = none;
    std::printf("%d %d %d %d %d %d %d %d\n", a, b, c, d, e, f, g, h);
    std::printf("%s %s %s %s\n", which(first), which(none), which(fn), which(noFn));
    std::printf("%d %d\n", static_cast<bool>(first) ? 1 : 0, !noFn ? 1 : 0);
    return 0;
}
