#include <cstdio>
struct One { virtual int a() { return 1; } virtual ~One() {} };
struct Two { virtual int b() = 0; virtual ~Two() {} };
struct Three { virtual int c() const = 0; virtual ~Three() {} };
struct D : One, Two, Three { int b() { return 2; } int c() const { return 3; } };
struct Top { virtual int f() = 0; virtual ~Top() {} };
struct L : virtual Top { int f() { return 4; } };
struct R : virtual Top {};
struct Joined : L, R {};
struct PureDtor { virtual ~PureDtor() = 0; };
PureDtor::~PureDtor() {}
struct Plain { int v; };
struct E : Plain, PureDtor { };
int main() {
    D d;
    Joined j;
    E e;
    Two &t = d;
    Top &top = j;
    (void)e;
    std::printf("%d %d %d %d\n", d.a(), t.b(), d.c(), top.f());
    return 0;
}
