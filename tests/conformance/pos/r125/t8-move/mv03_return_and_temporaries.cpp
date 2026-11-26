// [class.copy]/32: returning a local by name moves when it can; returning a parameter moves; returning a member copies; returning a reference-captured does not; conditional expression returns copy; braced return.
#include <cstdio>
#include <utility>
struct Tr { Tr() {} Tr(const Tr &) { std::printf("copy "); } Tr(Tr &&) { std::printf("move "); } };
Tr local() { Tr t; return t; }
Tr param(Tr t) { return t; }
struct Holder { Tr m; Tr get() { return m; } Tr take() { return std::move(m); } };
Tr cond(bool b) { Tr x, y; return b ? x : y; }
Tr global; Tr &ref() { return global; }
Tr fromRef() { Tr &r = ref(); return r; }
Tr explicitMove() { Tr t; return std::move(t); }
int main() {
    { Tr a = local(); } std::printf("| local\n");
    { Tr b = param(Tr()); } std::printf("| param\n");
    { Holder h; Tr c = h.get(); Tr d = h.take(); } std::printf("| member\n");
    { Tr e = cond(true); } std::printf("| cond\n");
    { Tr f = fromRef(); } std::printf("| ref\n");
    { Tr g = explicitMove(); } std::printf("| explicit\n");
    return 0;
}
