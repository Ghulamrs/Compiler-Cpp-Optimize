#include <cstdio>
struct A { int a = 1; virtual ~A() {} }; struct B { int b = 2; virtual ~B() {} }; struct C : A, B { int c = 3; };
int main() { C c; A *pa = &c; B *pb = &c; C *back = static_cast<C*>(pb); C *dyn = dynamic_cast<C*>(pb); A *cross = dynamic_cast<A*>(pb); std::printf("%d %d %d %d %d\n", (int)((void*)pb != (void*)&c), back->c, dyn->c, cross->a, pb->b); (void)pa; return 0; }
