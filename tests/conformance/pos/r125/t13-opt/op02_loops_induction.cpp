// Loop optimisations: strength reduction with overflow-free induction variables, nested loops summing, loop-carried dependence, reversed loops, loops with early exit, unsigned wrap loop, loop with floating accumulators, loop counting down with size_t.
#include <cstdio>
#include <cstddef>
int main() {
    long long s = 0; for (int i = 0; i < 1000; ++i) s += i * 3 + 1; std::printf("%lld\n", s);
    int m[10][10]; for (int i = 0; i < 10; ++i) for (int j = 0; j < 10; ++j) m[i][j] = i * j; int t = 0; for (int i = 0; i < 10; ++i) for (int j = 0; j < 10; ++j) t += m[i][j] * (i + j); std::printf("%d\n", t);
    int fib[50]; fib[0] = 0; fib[1] = 1; for (int i = 2; i < 50; ++i) fib[i] = fib[i - 1] + fib[i - 2]; std::printf("%d %d\n", fib[30], fib[45]);
    int arr[8] = {1, 2, 3, 4, 5, 6, 7, 8}; int r = 0; for (int i = 7; i >= 0; --i) r = r * 2 + arr[i]; std::printf("%d\n", r);
    int found = -1; for (int i = 0; i < 100; ++i) { if (i * i > 500) { found = i; break; } } std::printf("%d\n", found);
    unsigned char uc = 250; int wraps = 0; for (int i = 0; i < 10; ++i) { ++uc; if (uc == 0) ++wraps; } std::printf("%d %d\n", uc, wraps);
    double acc = 0; for (int i = 1; i <= 100; ++i) acc += 1.0 / i; std::printf("%.10f\n", acc);
    size_t n = 5; int cnt = 0; for (size_t i = n; i-- > 0;) ++cnt; std::printf("%d\n", cnt);
    int sq = 0; for (int i = 0; i < 20; i += 3) sq += i; std::printf("%d\n", sq);
    unsigned u = 0; for (unsigned i = 0xFFFFFFF0u; i != 0; ++i) ++u; std::printf("%u\n", u);
    int stride = 0; for (int i = 0, j = 100; i < j; i += 7, j -= 3) stride += j - i; std::printf("%d\n", stride);
    return 0;
}
