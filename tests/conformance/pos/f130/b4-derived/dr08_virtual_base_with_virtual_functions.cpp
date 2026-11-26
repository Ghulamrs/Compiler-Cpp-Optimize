#include <cstdio>
struct I { virtual int id() const = 0; virtual ~I() {} }; struct A : virtual I { int id() const override { return 1; } }; struct B : virtual I { }; struct C : A, B { };
int main() { C c; I &i = c; std::printf("%d\n", i.id()); return 0; }
