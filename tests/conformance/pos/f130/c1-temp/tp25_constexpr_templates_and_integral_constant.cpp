#include <cstdio>
#include <type_traits>
template <class T> constexpr T sq(T x) { return x * x; } template <class T, T v> struct IC { static constexpr T value = v; constexpr operator T() const { return v; } }; template <int N> struct Arr { int a[N]; }; template <bool B> int pick(std::integral_constant<bool, B>) { return B ? 1 : 2; }
int main() { Arr<sq(3)> a; static_assert(sq(4) == 16, "sq"); static_assert(IC<int, 5>::value == 5, "ic"); std::printf("%d %d %d %d\n", (int)sizeof(a) / 4, pick(std::true_type()), pick(std::is_pointer<int>()), (int)IC<int, 7>()); return 0; }
