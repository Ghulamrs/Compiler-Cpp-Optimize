// [dcl.fct]/5 and [dcl.ambig.res]: a parameter of function or array type is the pointer, and `int (int)` in a
// parameter list is an unnamed parameter of function type, not a parenthesised declarator.
#include <cstdio>
#include <type_traits>
typedef int T;
struct C { int get() { return 6; } };
int twice(int x) { return x * 2; }
int ap(int (int), int);
int ap(int g(int), int x) { return g(x); }
int k(int (x)) { return x + 1; }
int viaMember(int (C::*m)(), C &c) { return (c.*m)(); }
template <class S, class U> int viaTemplate(S (U::*m)(), U &u) { return (u.*m)(); }
void declared(int (T), int (*)(), void (...));
int main() {
    std::printf("%d", (int)std::is_same<void(int), void(const int)>::value);
    std::printf("%d", (int)std::is_same<void(int[]), void(int *)>::value);
    std::printf("%d", (int)std::is_same<void(int[3]), void(int *)>::value);
    std::printf("%d", (int)std::is_same<int(int(int)), int(int (*)(int))>::value);
    std::printf("%d\n", (int)std::is_same<int(int(*)), int(int *)>::value);
    typedef void F(int[]);
    typedef void G(int(int));
    std::printf("%d%d\n", (int)std::is_same<F, void(int *)>::value, (int)std::is_same<G, void(int (*)(int))>::value);
    C c;
    const int a = ap(twice, 4);
    const int b = k(1);
    const int d = viaMember(&C::get, c);
    const int e = viaTemplate(&C::get, c);
    std::printf("%d %d %d %d\n", a, b, d, e);
    return 0;
}
