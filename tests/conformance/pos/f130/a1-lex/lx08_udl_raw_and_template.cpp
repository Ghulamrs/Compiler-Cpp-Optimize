#include <cstdio>
int operator"" _len(const char *s) { int n = 0; while (s[n]) n++; return n; }
template <char... Cs> int operator"" _cnt() { return sizeof...(Cs); }
int main() { std::printf("%d %d\n", 12345_len, 0x1F_cnt); return 0; }
