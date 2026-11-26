#include <cstdio>
namespace { int hidden = 9; int fn() { return hidden; } } static int sfn() { return 1; }
namespace { namespace inner { int q = 2; } }
int main() { std::printf("%d %d %d\n", fn(), sfn(), inner::q); return 0; }
