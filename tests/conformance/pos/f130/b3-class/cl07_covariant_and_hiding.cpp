#include <cstdio>
struct B { virtual B *self() { return this; } int tag = 1; virtual ~B() {} void h(int) { std::printf("Bh "); } };
struct D : B { D *self() override { return this; } int tag2 = 2; void h(double) { std::printf("Dh "); } };
int main() { D d; B &b = d; D *p = d.self(); d.h(1); b.h(1); d.B::h(2); std::printf("%d %d\n", p->tag2, b.self()->tag); return 0; }
