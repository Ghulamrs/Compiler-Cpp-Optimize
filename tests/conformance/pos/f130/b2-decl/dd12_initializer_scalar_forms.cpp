#include <cstdio>
int main() { int a = 1; int b(2); int c{3}; int d = {4}; int e = int(5); int f = int{6}; double g(7); float h = {8.5f}; bool i(true); char j = {'j'}; long long k{9LL}; unsigned l = {10u}; int *m(nullptr); int n = (11); int o = {int(12)};
std::printf("%d %d %d %d %d %d %g %g %d %c %lld %u %p %d %d\n", a, b, c, d, e, f, g, h, (int)i, j, k, l, (void*)m, n, o); return 0; }
