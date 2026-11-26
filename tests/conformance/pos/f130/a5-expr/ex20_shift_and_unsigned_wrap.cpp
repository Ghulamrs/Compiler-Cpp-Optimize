#include <cstdio>
int main() { unsigned u = 0u - 1u; unsigned char c = 255; c++; int s = -8 >> 1; unsigned long long big = 1ull << 63; std::printf("%u %d %d %llu %d %d\n", u, c, s, big, 1 << 30, (int)(5u >> 1)); return 0; }
