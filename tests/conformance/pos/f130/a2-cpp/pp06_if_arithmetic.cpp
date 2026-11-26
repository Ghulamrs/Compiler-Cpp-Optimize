#include <cstdio>
#if (-1 < 0u)
#error "unsigned promotion in #if"
#endif
#if 0x7fffffffffffffff + 0 == 9223372036854775807
int a = 1;
#else
int a = 0;
#endif
#if 'A' == 65 && '\n' == 10
int b = 1;
#else
int b = 0;
#endif
#if 1 ? 2 : (1/0)
int c = 1;
#else
int c = 0;
#endif
#if defined(X) || !defined X
int d = 1;
#else
int d = 0;
#endif
#if true
int e = 1;
#else
int e = 0;
#endif
#if UNDEFINED_NAME == 0
int f = 1;
#else
int f = 0;
#endif
int main() { std::printf("%d %d %d %d %d %d\n", a, b, c, d, e, f); return 0; }
