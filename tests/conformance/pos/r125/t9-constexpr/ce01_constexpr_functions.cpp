// [dcl.constexpr]: recursion, conditional, calls to other constexpr functions, used at compile time (array bound, template arg, static_assert) and at run time with non-constant args; implicit const on constexpr member functions in C++11 (not relied on).
#include <cstdio>
constexpr int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
constexpr int gcd(int a, int b) { return b == 0 ? a : gcd(b, a % b); }
constexpr long long pow2(int n) { return n == 0 ? 1LL : 2LL * pow2(n - 1); }
constexpr double half(double d) { return d / 2; }
constexpr int absv(int x) { return x < 0 ? -x : x; }
constexpr bool isPrime(int n, int d = 2) { return d * d > n ? true : (n % d == 0 ? false : isPrime(n, d + 1)); }
constexpr const char *pick(bool b) { return b ? "yes" : "no"; }
constexpr int strlenC(const char *s) { return *s ? 1 + strlenC(s + 1) : 0; }
template <int N> struct V { static const int v = N; };
int main() {
    static_assert(fact(5) == 120, "fact"); static_assert(gcd(48, 18) == 6, "gcd"); static_assert(pow2(40) == (1LL << 40), "pow2"); static_assert(isPrime(97), "prime");
    int arr[fact(4)]; (void)arr;
    std::printf("%d %d %d %lld %g\n", (int)sizeof(arr) / 4, V<gcd(100, 75)>::v, fact(6), pow2(10), half(5));
    int n = 7; std::printf("%d %d %d %d\n", fact(n), gcd(n, 21), absv(-n), isPrime(n));
    std::printf("%s %d\n", pick(fact(3) == 6), strlenC("hello"));
    static_assert(strlenC("abc") == 3, "len");
    constexpr int c = absv(-9); std::printf("%d\n", c);
    enum { EV = fact(3) }; std::printf("%d\n", EV);
    switch (n) { case fact(3) + 1: std::printf("seven\n"); break; default: std::printf("other\n"); }
    return 0;
}
