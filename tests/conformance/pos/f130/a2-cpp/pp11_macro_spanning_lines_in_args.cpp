#include <cstdio>
#define SUM(a, b, c) ((a) + (b) + (c))
int main() { int s = SUM(1,
  2,
  3); std::printf("%d %d\n", s, __LINE__); return 0; }
