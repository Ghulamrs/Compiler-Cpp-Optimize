// Constexpr functions used with runtime values and pointers; constexpr with references and arrays; a constexpr function with a static_assert (via template) in its signature; address of constexpr function; constexpr in lambda-free loops.
#include <cstdio>
constexpr int sumArr(const int *a, int n) { return n == 0 ? 0 : a[n - 1] + sumArr(a, n - 1); }
constexpr int maxOf(int a, int b) { return a > b ? a : b; }
constexpr int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
constexpr int arr[] = {1, 2, 3, 4};
constexpr int total = sumArr(arr, 4);
template <int N> constexpr int times(int v) { return v * N; }
constexpr int (*fp)(int, int) = maxOf;
int main() {
    static_assert(total == 10, "sum"); static_assert(times<3>(4) == 12, "times");
    int runtime[] = {5, 6, 7}; std::printf("%d %d\n", sumArr(runtime, 3), total);
    int s = 0; for (int i = 0; i < 10; ++i) s += clamp(i, 2, 6); std::printf("%d\n", s);
    std::printf("%d %d\n", fp(2, 9), maxOf(runtime[0], runtime[1]));
    constexpr int cmax = maxOf(times<2>(5), 8); std::printf("%d\n", cmax);
    return 0;
}
