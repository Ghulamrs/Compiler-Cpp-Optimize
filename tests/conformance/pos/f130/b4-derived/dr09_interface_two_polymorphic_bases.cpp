#include <cstdio>
struct IA { virtual int a() const = 0; virtual ~IA() {} }; struct IB { virtual int b() const = 0; virtual ~IB() {} }; struct Both : IA, IB { int a() const override { return 1; } int b() const override { return 2; } };
int main() { Both x; IA &ia = x; IB &ib = x; std::printf("%d %d\n", ia.a(), ib.b()); return 0; }
