#include <cstdio>
int main() { double d = 3.99; int i = d; int j = -3.99; unsigned u = 3.5; float f = 16777217; long long ll = 1e18; char c = 300.0 > 299.0 ? 'y' : 'n'; std::printf("%d %d %u %.0f %lld %c\n", i, j, u, f, ll, c); return 0; }
