#include <cstdio>
extern "C" int cfunc(int x) { return x + 1; }
extern "C" { int cf2(int x); typedef int (*cfp)(int); }
int cf2(int x) { return x * 2; }
int main() { cfp p = cfunc; std::printf("%d %d\n", p(1), cf2(3)); return 0; }
