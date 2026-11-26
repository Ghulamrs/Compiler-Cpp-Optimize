// Which constexpr static_asserts fail: a plain recursive one, one with a long long result, one with a default argument, one returning const char*.
#include <cstdio>
constexpr int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
constexpr long long pow2(int n) { return n == 0 ? 1LL : 2LL * pow2(n - 1); }
constexpr bool isPrime(int n, int d = 2) { return d * d > n ? true : (n % d == 0 ? false : isPrime(n, d + 1)); }
constexpr int strlenC(const char *s) { return *s ? 1 + strlenC(s + 1) : 0; }
static_assert(fact(5) == 120, "fact");
static_assert(pow2(40) == (1LL << 40), "pow2");
static_assert(isPrime(97), "prime");
static_assert(strlenC("abc") == 3, "len");
int main() { std::printf("%d %lld %d %d\n", fact(5), pow2(40), isPrime(97), strlenC("abc")); return 0; }
