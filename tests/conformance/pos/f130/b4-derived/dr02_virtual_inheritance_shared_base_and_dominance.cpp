#include <cstdio>
struct V { int v = 7; int f() { return 1; } V() { std::printf("V "); } }; struct L : virtual V { int f() { return 2; } L() { std::printf("L "); } }; struct R : virtual V { R() { std::printf("R "); } }; struct D : L, R { D() { std::printf("D "); } };
int main() { D d; d.v = 9; V &v = d; std::printf("%d %d %d %d\n", v.v, d.f(), d.R::v, (int)(&d.L::v == &d.R::v)); return 0; }
