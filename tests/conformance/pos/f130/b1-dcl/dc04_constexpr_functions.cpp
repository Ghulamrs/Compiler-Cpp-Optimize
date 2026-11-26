#include <cstdio>
constexpr int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
constexpr int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
constexpr const int *pick(const int *a, const int *b, bool f) { return f ? a : b; }
constexpr int arr[] = {1, 2, 3};
constexpr int sum(const int *p, int n) { return n == 0 ? 0 : *p + sum(p + 1, n - 1); }
struct P { int x, y; constexpr P(int a, int b) : x(a), y(b) {} constexpr int prod() const { return x * y; } };
constexpr P mk(int a) { return P(a, a + 1); }
int main() { static_assert(fact(5) == 120, "f"); static_assert(fib(10) == 55, "fib"); static_assert(mk(3).prod() == 12, "p"); static_assert(sum(arr, 3) == 6, "sum"); constexpr const int *q = pick(arr, arr + 1, false); static_assert(*q == 2, "q");
int n = 6; std::printf("%d %d %d %d\n", fact(n), fib(n), mk(n).x, *q); return 0; }
