#include <cstdio>
namespace N { int f(int) { return 1; } } namespace M { int f(const char *) { return 2; } } using N::f; using M::f;
int main() { std::printf("%d %d\n", f(1), f("x")); return 0; }
