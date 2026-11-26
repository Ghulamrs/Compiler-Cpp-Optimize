#include <cstdio>
#include <utility>
struct C { static int copies, moves; C() {} C(const C &) { copies++; } C(C &&) { moves++; } }; int C::copies = 0; int C::moves = 0;
C byValue(C c) { return c; } C nrvo() { C c; return c; } C prvalue() { return C(); }
int main() { C a; C b = byValue(a); C c = nrvo(); C d = prvalue(); C e = std::move(a); std::printf("%d %d\n", C::copies, C::moves >= 1 ? 1 : 0); (void)b; (void)c; (void)d; (void)e; return 0; }
