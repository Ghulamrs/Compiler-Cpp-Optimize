// [temp.inst]/3: a constructor template's definition is instantiated where it is needed, and an unevaluated operand or
// a trait needs only its declaration; a candidate not chosen is not needed either.
#include <cstdio>
#include <type_traits>
struct W { template <class U> W(U) { typename U::type t; (void)t; } W(int) {} };
struct X { int v; template <class U> X(U u) : v(u.nothing) {} X(int a) : v(a) {} };
struct Y { template <class U> Y(const U &u) { u.nothing(); } Y() {} };
struct Z { template <class U> Z(U) { static_assert(sizeof(U) == 0, "not for this"); } Z(int) {} };
int main() {
    std::printf("%d %d %d %d\n", (int)std::is_constructible<W, double>::value, (int)std::is_convertible<double, W>::value,
                (int)std::is_constructible<Y, char>::value, (int)std::is_constructible<Z, double>::value);
    W a(1);
    X b(2);
    Z c(3);
    (void)a;
    (void)c;
    std::printf("%d %d %d\n", (int)sizeof(W(1.5)) == (int)sizeof(W), (int)noexcept(X(2.5)), b.v);
    return 0;
}
