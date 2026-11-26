#include <cstdio>
struct V { int v = 7; }; struct W { int w = 8; };
struct L : virtual V { int l = 1; }; struct R : virtual V, virtual W { int r = 2; };
struct B : L, R { int b = 3; };
struct D : virtual B { int d = 4; };
struct F : B { int f = 5; };
struct G : virtual D, F { int g = 6; };
int main() {
  F f; f.v = 11; f.w = 12; R &rr = f; std::printf("%d %d %d %d ", f.v, rr.v, rr.w, f.B::r);
  D d; d.v = 13; B &rb = d; L &rl = d; R &rr2 = d; std::printf("%d %d %d %d %d ", d.v, rb.v, rl.v, rr2.v, rr2.w);
  G g; g.F::v = 21; D &gd = g; F &gf = g; std::printf("%d %d %d %d\n", gd.v, gf.v, gd.B::r, gf.R::w);
  return 0;
}
