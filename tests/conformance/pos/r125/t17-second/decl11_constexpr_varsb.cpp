// [dcl.constexpr]: constexpr variables of scalar, array, pointer-to-function, class literal types; used as array bounds, template arguments, case labels, enum values.
#include <cstdio>
constexpr int N = 4;
constexpr double PI = 3.25;
constexpr int sq(int x) { return x * x; }
const int arrN[3] = {1, 2, 3};
struct Lit { constexpr Lit(int a, int b) : x(a), y(b) {} int x, y; constexpr int sum() const { return x + y; } };
constexpr Lit L(2, 3);
constexpr int (*fp)(int) = sq;
template <int K> struct Val { static const int v = K; };
enum E { E0 = sq(3), E1 = N * 2 };
int main() {
    int arr[sq(N)] = {0}; arr[15] = 1;
    std::printf("%d %d %d\n", (int)sizeof(arr) / (int)sizeof(int), Val<sq(5)>::v, Val<L.sum()>::v);
    std::printf("%d %d %g\n", E0, E1, PI * 2);
    switch (N) { case sq(2): std::printf("case4\n"); break; default: std::printf("no\n"); }
    std::printf("%d %d %d\n", arrN[2], L.x, fp(6));
    constexpr int local = N + 1; int arr2[local]; (void)arr2;
    std::printf("%d %d\n", local, (int)sizeof(arr2) / 4);
    constexpr const char *s = "lit"; std::printf("%s\n", s);
    const int &r = N; std::printf("%d\n", r);
    return 0;
}
