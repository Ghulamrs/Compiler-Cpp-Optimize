#include <cstdio>
struct B { virtual int f() = delete; virtual int g() { return 1; } virtual int h() { return 2; } virtual ~B() {} };
struct D : B { int f() = delete; int g() { return 10; } };
struct E : D { int h() { return 20; } };
int main() {
    E e;
    B *b = &e;
    D d;
    B &r = d;
    std::printf("%d %d %d %d\n", b->g(), b->h(), r.g(), r.h());
    return 0;
}
