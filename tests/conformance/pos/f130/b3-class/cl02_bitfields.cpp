#include <cstdio>
enum class Col : unsigned char { r, g, b }; struct F { unsigned a : 3; int : 2; unsigned b : 5; int : 0; unsigned c : 1; Col col : 2; bool flag : 1; long long big : 40; };
int main() { F f{}; f.a = 7; f.b = 31; f.c = 1; f.col = Col::b; f.flag = true; f.big = -5; f.a += 1; ++f.b; std::printf("%u %u %u %d %d %lld %d\n", f.a, f.b, f.c, (int)f.col, (int)f.flag, f.big, (int)sizeof(F) >= 8); return 0; }
