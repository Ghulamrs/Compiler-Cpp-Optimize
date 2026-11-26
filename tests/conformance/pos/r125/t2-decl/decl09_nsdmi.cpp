// [class.mem]/9: non-static data member initialisers, their interaction with mem-initialisers (mem-init wins), with = default ctor, with other members, in base classes, brace and paren forms.
#include <cstdio>
struct A { int x = 5; int y{6}; int z = x + y; double d = 1.5; const char *s = "nsdmi"; };
struct B { int x = 1; B() {} B(int v) : x(v) {} };
struct C { int a = 7; C() = default; };
struct Base { int b = 3; };
struct Derived : Base { int d = b + 1; };
struct Arr { int arr[3] = {1, 2, 3}; int n = arr[2]; };
struct Count { static int made; int id = ++made; };
int Count::made = 0;
struct Ptr { int *p = nullptr; int v = 4; int *q = &v; };
int main() {
    A a; std::printf("%d %d %d %g %s\n", a.x, a.y, a.z, a.d, a.s);
    B b1, b2(9); std::printf("%d %d\n", b1.x, b2.x);
    C c; std::printf("%d\n", c.a);
    Derived d; std::printf("%d %d\n", d.b, d.d);
    Arr r; std::printf("%d %d\n", r.arr[1], r.n);
    Count c1, c2, c3; std::printf("%d %d %d %d\n", c1.id, c2.id, c3.id, Count::made);
    Ptr p; std::printf("%d %d\n", p.p == nullptr, *p.q);
    A a2 = a; a2.x = 100; std::printf("%d %d\n", a.x, a2.z);
    return 0;
}
