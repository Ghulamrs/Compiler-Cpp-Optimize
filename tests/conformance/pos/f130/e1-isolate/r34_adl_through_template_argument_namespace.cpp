#include <cstdio>
namespace A { struct X {}; } namespace B { template <class T> struct W {}; } namespace A { int who(B::W<X>) { return 1; } }
int main() { B::W<A::X> w; std::printf("%d\n", who(w)); return 0; }
