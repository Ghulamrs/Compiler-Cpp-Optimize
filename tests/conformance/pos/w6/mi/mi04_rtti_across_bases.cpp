// [expr.dynamic.cast]/8, [expr.typeid]: cross casts, downcasts, void* and typeid through the second base; a private base is not a target
#include <cstdio>
#include <typeinfo>
struct I1 { virtual ~I1() {} int i1; };
struct I2 { virtual ~I2() {} int i2; };
struct X : I1, I2 { int x; };
struct P1 { virtual ~P1() {} };
struct Q : P1, private I2 {};
int main() {
    X x; I2 *p2 = &x; I1 *p1 = &x;
    I1 *cross = dynamic_cast<I1 *>(p2);
    X *down = dynamic_cast<X *>(p2);
    void *whole = dynamic_cast<void *>(p2);
    std::printf("%d %d %d %d %d\n", cross == p1, down == &x, whole == (void *)&x, typeid(*p2) == typeid(X), typeid(*p1) == typeid(X));
    Q q; P1 *pq = &q;
    std::printf("%d\n", dynamic_cast<I2 *>(pq) == 0);
    return 0;
}
