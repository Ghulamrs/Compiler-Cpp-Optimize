// The built-ins where <type_traits> uses them: a class template's base and members replayed with T bound, a
// pack expanded into __is_constructible, __underlying_type as a type, and a trait in a function body.
#include <cstdio>
template <class T, T v> struct constant { static const T value = v; };
template <class T> struct is_enum_ : constant<bool, __is_enum(T)> {};
template <class T> struct is_pod_ { static const bool value = __is_pod(T); };
template <class T, class... A> struct is_constructible_ : constant<bool, __is_constructible(T, A...)> {};
template <class T> struct underlying_ { typedef __underlying_type(T) type; };
template <class From, class To> struct is_convertible_ : constant<bool, __is_convertible_to(From, To)> {};
template <class B, class D> struct is_base_of_ : constant<bool, __is_base_of(B, D)> {};
enum Small : unsigned char { s1 = 200 };
enum class Wide : long long { w1 = 1 };
enum Plain { p1 };
struct S { S(int, char) {} };
struct Pod { int a; };
struct D : Pod {};
template <class T> int describe() { return (__is_class(T) ? 100 : 0) + (__is_enum(T) ? 10 : 0) + (__is_pod(T) ? 1 : 0); }
template <class T, class... A> bool make() { return __is_constructible(T, A...) && __is_constructible(T, A &&...); }
int main() {
    std::printf("%d%d%d ", (int)is_enum_<Small>::value, (int)is_enum_<int>::value, (int)is_enum_<const Wide>::value);
    std::printf("%d%d ", (int)is_pod_<Pod>::value, (int)is_pod_<S>::value);
    std::printf("%d%d%d%d ", (int)is_constructible_<S, int, char>::value, (int)is_constructible_<S, int>::value,
                (int)is_constructible_<S>::value, (int)is_constructible_<int, long>::value);
    underlying_<Small>::type u = 255;
    underlying_<Wide>::type w = 1LL << 40;
    std::printf("%d %d %lld %d ", (int)sizeof(underlying_<Small>::type), (int)u, (long long)(w >> 40),
                (int)sizeof(__underlying_type(Wide)));
    std::printf("%d%d%d%d ", (int)is_convertible_<D *, Pod *>::value, (int)is_convertible_<Pod *, D *>::value,
                (int)is_base_of_<Pod, D>::value, (int)is_base_of_<D, Pod>::value);
    std::printf("%d %d %d ", describe<Pod>(), describe<Plain>(), describe<S>());
    std::printf("%d%d%d\n", (int)make<S, int, char>(), (int)make<S, int>(), (int)make<int, long>());
}