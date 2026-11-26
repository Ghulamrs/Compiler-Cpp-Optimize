#include <cstdio>
namespace A { struct X {}; int tag(X) { return 1; } }
namespace B { template <class T> struct W {}; }
namespace C { int who(B::W<A::X>) { return 7; } }
namespace A { int who(B::W<X>) { return 1; } }
int main() { B::W<A::X> w; std::printf("%d\n", who(w)); return 0; }
