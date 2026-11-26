// [expr.dynamic.cast]/9: a failed cast to a reference throws std::bad_cast, which is a std::exception
#include <cstdio>
#include <typeinfo>
struct B { virtual ~B() {} }; struct D : B { int v; D() : v(7) {} }; struct E : B {};
int main() {
    D d; E e; B &bd = d, &be = e;
    try { D &r = dynamic_cast<D &>(bd); std::printf("ok%d ", r.v); D &s = dynamic_cast<D &>(be); std::printf("no%d ", s.v); }
    catch (const std::bad_cast &) { std::printf("bad_cast "); }
    try { (void)dynamic_cast<D &>(be); }
    catch (std::exception &x) { std::printf("exception%d\n", x.what() != 0); }
    return 0;
}
