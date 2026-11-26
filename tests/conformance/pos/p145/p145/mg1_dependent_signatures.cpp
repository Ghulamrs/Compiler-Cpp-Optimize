// [temp.dep.constexpr] and [temp.over.link]: expressions over the parameters in a function template's
// signature - `sizeof(T)` as an array bound that removes an overload by substitution failure, a qualified
// call and `std::declval<T>()` in a trailing decltype - read as patterns and again with the arguments.
#include <cstdio>
#include <utility>
namespace N {
    struct K { static int get(int v) { return v * 3; } static int over(int v) { return v; } static double over(double v) { return v / 2; } };
    int free1(int v) { return v + 1; }
}
struct Str { int size() const { return 7; } };
template <class T> int val(T, char (*)[sizeof(T) == 4] = 0) { return 4; }
template <class T> int val(T, char (*)[sizeof(T) == 8] = 0) { return 8; }
template <class T> char (&sz(T))[sizeof(T)];
template <class T> char (&sz2(T))[sizeof(T) + 1] { static char c[sizeof(T) + 1]; return c; }
template <class T> auto viaK(T t) -> decltype(N::K::get(t)) { return N::K::get(t); }
template <class T> auto viaO(T t) -> decltype(N::K::over(t)) { return N::K::over(t); }
template <class T> auto viaF(T t) -> decltype(N::free1(t)) { return N::free1(t); }
template <class T> auto sizeOf(T t) -> decltype(std::declval<T>().size()) { return t.size(); }
template <class T> void may(T) noexcept(noexcept(T()));
template <class T> void may(T) noexcept(noexcept(T())) {}
struct Thr { Thr() noexcept(false) {} };
int main() {
    std::printf("%d %d %d %d\n", val(1), val(1.0), (int)sizeof(sz('a')), (int)sizeof(sz2(1.0)));
    std::printf("%d %g %g %d %d\n", viaK(2), (double)viaO(3), (double)viaO(3.0), viaF(4), sizeOf(Str()));
    std::printf("%d %d %d\n", (int)noexcept(may(1)), (int)noexcept(may(Thr())), sz2(1)[4] == 0);
    return 0;
}
