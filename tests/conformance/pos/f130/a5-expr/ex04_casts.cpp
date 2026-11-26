#include <cstdio>
struct B { virtual ~B() {} int b = 1; }; struct D : B { int d = 2; }; struct X { virtual ~X() {} int x = 3; }; struct M : X, B {};
int main() { M m; B *pb = &m; void *pv = dynamic_cast<void*>(pb); X *px = dynamic_cast<X*>(pb); D *pd = dynamic_cast<D*>(pb); const int ci = 5; int *pi = const_cast<int*>(&ci); void (*fp)() = reinterpret_cast<void(*)()>(&m); int (*ip)() = reinterpret_cast<int(*)()>(fp);
std::printf("%d %d %d %d %d\n", (int)(pv == static_cast<void*>(&m)), (int)(px == &m), (int)(pd == nullptr), *pi, (int)(ip != nullptr)); return 0; }
