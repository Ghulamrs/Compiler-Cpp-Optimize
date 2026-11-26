#include <cstdio>
#include <new>
struct S { int v; S(int v) : v(v) {} ~S() { std::printf("~%d ", v); } };
struct Virt { virtual ~Virt() = 0; }; Virt::~Virt() { std::printf("~Virt "); } struct Impl : Virt { ~Impl() override { std::printf("~Impl "); } };
int main() { { S a(1), b(2); S c{3}; } { Impl i; } S *p = new S(4); p->~S(); ::operator delete(p); S s(5); s.~S(); new (&s) S(6); std::printf("\n"); return 0; }
