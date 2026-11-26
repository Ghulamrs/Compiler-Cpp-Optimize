#include <cstdio>
struct V { int v = 7; }; struct L : virtual V {}; struct R : virtual V {}; struct D : L, R {};
int main() { D d; d.v = 9; R &r = d; L &l = d; std::printf("%d %d %d %d\n", d.L::v, d.R::v, r.v, l.v); return 0; }
