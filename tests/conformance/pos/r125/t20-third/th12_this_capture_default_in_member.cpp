// [=] and [&] inside a member function capture this implicitly; the closure sees members through it.
#include <cstdio>
struct W { int v = 3; int byCopy() const { auto l = [=] { return v * 2; }; return l(); } int byRef() { auto l = [&] { v += 1; return v; }; return l(); } };
int main() { W w; std::printf("%d %d %d\n", w.byCopy(), w.byRef(), w.v); return 0; }
