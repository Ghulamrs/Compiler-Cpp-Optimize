#include <cstdio>
struct W { int i; double d; W(int v) : i{v}, d{1.5} {} };
int main() { W w(5); std::printf("%d %g\n", w.i, w.d); return 0; }
