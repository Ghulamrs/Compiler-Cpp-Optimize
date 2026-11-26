#include <cstdio>
namespace N { int f(int) { return 1; } int f(double) { return 2; } struct S {}; int g(S) { return 3; } } namespace M { int f(const char *) { return 4; } }
using N::f; using M::f; using N::S; using N::g;
int main() { S s; std::printf("%d %d %d %d\n", f(1), f(1.0), f("x"), g(s)); return 0; }
