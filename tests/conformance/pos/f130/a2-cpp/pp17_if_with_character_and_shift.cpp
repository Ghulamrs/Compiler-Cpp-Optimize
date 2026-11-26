#include <cstdio>
#if (1 << 40) == 1099511627776
int a = 1;
#else
int a = 0;
#endif
#if -1 >> 1 == -1
int b = 1;
#else
int b = 0;
#endif
#if 10 % 3 == 1 && (3 > 2 ? 7 : 8) == 7 && ~0 == -1 && !0
int c = 1;
#else
int c = 0;
#endif
int main() { std::printf("%d %d %d\n", a, b, c); return 0; }
