// [class.virtual]/2: one function overriding a virtual of each base; the second base's table reaches it through an adjustor
#include <cstdio>
struct A { int a; A() : a(1) {} virtual int f(int k) { return k; } };
struct B { int b; B() : b(2) {} virtual int f(int k) { return -k; } virtual int g() { return b; } };
struct D : A, B { int d; D() : d(30) {} int f(int k) { return d + k + a * 100 + b * 1000; } };
int main() {
    D x; A *pa = &x; B *pb = &x;
    std::printf("%d %d %d %d\n", pa->f(1), pb->f(2), pb->g(), x.f(3));
    return 0;
}
