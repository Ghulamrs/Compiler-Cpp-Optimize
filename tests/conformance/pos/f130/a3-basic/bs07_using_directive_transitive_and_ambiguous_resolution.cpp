#include <cstdio>
namespace A { int x = 1; } namespace B { using namespace A; int y = 2; }
namespace C { int z = 3; }
int main() { using namespace B; using namespace C; std::printf("%d %d %d\n", x, y, z); return 0; }
