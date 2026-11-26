#include <cstdio>
namespace Very { namespace Long { namespace Name { int v = 3; } } } namespace VLN = Very::Long::Name; namespace Very { namespace Long { namespace Name { int w = 4; } } }
namespace A { namespace B { int x = 1; } } namespace C = A::B; namespace D = C;
int main() { std::printf("%d %d %d %d\n", VLN::v, VLN::w, C::x, D::x); return 0; }
