#include <cstdio>
struct V { int v = 7; }; struct L : virtual V { int l = 1; }; struct R : virtual V { int r = 2; }; struct D : L, R { int d = 3; };
struct X { int x = 40; }; struct E : X, virtual D { int e = 5; };
int main() { E e; e.v = 13; D &rd = e; std::printf("%d %d %d\n", rd.v, rd.L::v, rd.V::v); return 0; }
