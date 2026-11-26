#include <cstdio>
#if 0 && (1 / 0)
#error "the right of a false && was evaluated"
#endif
#if 1 || (1 % 0)
int a = 1;
#endif
#if 0 ? 1 / 0 : 2
int b = 2;
#endif
#if (0 && (0x7fffffffffffffff + 1)) == 0
int c = 3;
#endif
#if -1 < 0u
#error "a negative value against an unsigned one"
#endif
#if (2 || 1 / 0) == 1 && (1 ? 2 : 1 / 0) == 2
int d = 4;
#endif
#if 0x8000000000000000 > 0 && ~0u == 0xffffffffffffffff
int e = 5;
#endif
#if (-9223372036854775807 - 1) / 2 == -4611686018427387904 && -7 / 2 == -3 && -7 % 2 == -1
int f = 6;
#endif
#if (1 << 62) == 4611686018427387904 && 07777 == 4095 && 0X1fULL == 31
int g = 7;
#endif
#if 1 and not 0 and (3 bitand 1) and (2 xor 3) == 1 and (0 or 1) and 5 not_eq 6
int h = 8;
#endif
int main() { std::printf("%d %d %d %d %d %d %d %d\n", a, b, c, d, e, f, g, h); return 0; }
