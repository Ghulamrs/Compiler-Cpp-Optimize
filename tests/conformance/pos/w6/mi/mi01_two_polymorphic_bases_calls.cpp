// [class.mi], [class.virtual]: overriders from two polymorphic bases, reached through either base, the derived class, a qualified call and delete through the second base
#include <cstdio>
struct I1 { int i1; I1() : i1(1) {} virtual int a() { return 1; } virtual ~I1() { std::printf("~I1 "); } };
struct I2 { int i2; I2() : i2(2) {} virtual int b() { return 2; } virtual int c() const { return 3 + i2; } virtual ~I2() { std::printf("~I2 "); } };
struct X : I1, I2 { int x; X() : x(40) {} int a() { return 10 + x; } int b() { return 20 + x; } virtual int d() { return x + i1 + i2; } ~X() { std::printf("~X "); } };
int main() {
    X *p = new X;
    I1 *q1 = p; I2 *q2 = p;
    std::printf("%d %d %d %d %d %d %d\n", q1->a(), q2->b(), q2->c(), p->b(), p->X::b(), p->I2::b(), p->d());
    delete q2;
    std::printf("\n");
    return 0;
}
