#include <cstdio>
struct A { explicit A(int) {} }; struct B { B(int) {} }; struct C { explicit operator int() const { return 4; } operator double() const { return 1.5; } };
void fa(A) {} void fb(B) {}
int main() { A a(1); A a2{2}; B b = 3; B b2 = {4}; fb(5); fb({6}); fa(A(7)); C c; int i = static_cast<int>(c); double d = c; int j(c); std::printf("%d %g %d\n", i, d, j); (void)a; (void)a2; (void)b; (void)b2; return 0; }
