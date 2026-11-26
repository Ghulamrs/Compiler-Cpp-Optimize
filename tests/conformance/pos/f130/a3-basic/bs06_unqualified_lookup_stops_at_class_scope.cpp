#include <cstdio>
int f(double) { return 1; }
struct S { int f(int) { return 2; } int g() { return f(1.5); } };
namespace N { int h(double) { return 3; } namespace M { int h(int) { return 4; } int call() { return h(1.5); } } }
int main() { S s; std::printf("%d %d\n", s.g(), N::M::call()); return 0; }
