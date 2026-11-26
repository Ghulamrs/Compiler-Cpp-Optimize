#include <cstdio>
struct B { virtual ~B() { std::printf("~B "); } }; struct D : B { ~D() override { std::printf("~D "); } };
struct E { int i; static int next; E() : i(next++) {} ~E() { std::printf("%d", i); } }; int E::next = 0;
int main() { B *b = new D; delete b; E *e = new E[3]; delete[] e; std::printf("\n"); return 0; }
