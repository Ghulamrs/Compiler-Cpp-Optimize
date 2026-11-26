#include <cstdio>
struct B { int f(int) { return 1; } }; struct D : B { using B::f; int f(double) { return 2; } };
int main() { D d; std::printf("%d %d\n", d.f(1), d.f(1.0)); return 0; }
