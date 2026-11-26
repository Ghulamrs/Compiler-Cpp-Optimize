#include <cstdio>
struct B { int b = 1; }; struct D : B { int d = 2; };
int main() { D d; const B &cb = static_cast<const B &>(d); B &b = static_cast<B &>(d); D &back = static_cast<D &>(b); std::printf("%d %d %d\n", cb.b, b.b, back.d); return 0; }
