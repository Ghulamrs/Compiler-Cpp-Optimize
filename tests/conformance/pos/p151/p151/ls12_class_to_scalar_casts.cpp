// [expr.static.cast]/4, [expr.cast]/4, [meta.unary.prop]: a class reaches a scalar through its conversion
// functions - explicit, templated, to a type that converts on - and is_constructible says so; a tuple's
// converting copy and move are chosen past its element constructor.
#include <cstdio>
#include <tuple>
#include <type_traits>
struct I { operator int() const { return 7; } };
struct Bo { explicit operator bool() const { return true; } };
struct T { template <class U> operator U() const { return U(3); } };
struct P { operator void *() const { return (void *)16; } };
enum E { e0, e1 };
struct En { operator E() const { return e1; } };
struct Q { int v; };
int main() {
    I i; Bo b; T t; P p; En en;
    long a = static_cast<long>(i);
    int c = (int)static_cast<bool>(b);
    double d = static_cast<double>(t);
    char *q = static_cast<char *>(static_cast<void *>(p));
    long f = long(i);
    int g = (int)i;
    int h = static_cast<int>(en);
    std::printf("%ld %d %g %d %ld %d %d\n", a, c, d, (int)(q != 0), f, g, h);
    std::printf("%d %d %d\n", (int)std::is_constructible<long, Q &>::value, (int)std::is_constructible<long, I &>::value,
                (int)std::is_convertible<Q, long>::value);
    std::tuple<int> x(1);
    std::tuple<long> y(x);
    std::tuple<long> z(std::tuple<int>(2));
    std::printf("%ld %ld\n", std::get<0>(y), std::get<0>(z));
    return 0;
}
