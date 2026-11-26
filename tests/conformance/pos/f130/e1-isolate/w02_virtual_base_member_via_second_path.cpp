#include <cstdio>
struct V { int v = 7; }; struct L : virtual V {}; struct R : virtual V {}; struct D : L, R {};
int main() { D d; d.v = 9; std::printf("%d %d %d %d\n", d.L::v, d.R::v, static_cast<R&>(d).v, static_cast<L&>(d).v); return 0; }
