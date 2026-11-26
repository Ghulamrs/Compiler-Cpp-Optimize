// [expr.const]: integral constant expressions in array bounds, case labels, bit-field widths, enumerators, template args: sizeof, alignof, casts, conditional, comma inside parens (allowed in C++11), char literals, enum arithmetic, const int with constant init, addresses of statics as constant address expressions.
#include <cstdio>
const int CI = 10;
enum E { A = 1, B = A << 3, C = B | A, D = sizeof(int) * 2, F = (int)'z' };
struct Bits { unsigned a : CI / 2; unsigned b : (B > 4 ? 3 : 1); };
template <int N> struct W { static const int v = N; };
int gvar; int *const gp = &gvar;
const char *const cs = "lit";
int main() {
    int a1[CI]; int a2[sizeof(long long) + alignof(double)]; int a3[(1, 5)]; int a4[static_cast<int>(2.9) + 1]; int a5[A + B + C]; int a6[F - 'a' + 1];
    std::printf("%d %d %d %d %d %d\n", (int)sizeof(a1) / 4, (int)sizeof(a2) / 4, (int)sizeof(a3) / 4, (int)sizeof(a4) / 4, (int)sizeof(a5) / 4, (int)sizeof(a6) / 4);
    std::printf("%d %d %d %d\n", W<CI * 2>::v, W<(CI > 5)>::v, W<-CI>::v, W<D>::v);
    Bits b; b.a = 31; b.b = 7; std::printf("%u %u\n", b.a, b.b);
    int x = 9; switch (x) { case CI - 1: std::printf("nine "); break; case C + 1: std::printf("C "); break; default: std::printf("def "); }
    switch (x) { case (int)(CI * 0.9): std::printf("float-case\n"); break; default: std::printf("no\n"); }
    std::printf("%d\n", gp == &gvar);
    const int local = x * 2; int arrLocal[CI]; (void)arrLocal; std::printf("%d\n", local);
    std::printf("%d %d\n", (int)sizeof(char[W<3>::v]), (int)sizeof(int[B]));
    std::printf("%s\n", cs);
    return 0;
}
