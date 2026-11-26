#include <cstdio>
struct S { int tag; union { int i; double d; char c[8]; }; S() : tag(1), i(0) {} };
static union { int gi; float gf; };
namespace N { static union { int ni; char nc; }; }
int main() { S s; s.d = 2.5; gi = 3; N::nc = 'k'; std::printf("%d %g %d %c %d\n", s.tag, s.d, gi, N::nc, (int)sizeof(S)); return 0; }
