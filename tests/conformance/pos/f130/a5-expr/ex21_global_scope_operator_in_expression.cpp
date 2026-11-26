#include <cstdio>
int v = 1; namespace N { int v = 2; int f() { return ::v + v; } }
int main() { std::printf("%d %d\n", N::f(), ::N::v); return 0; }
