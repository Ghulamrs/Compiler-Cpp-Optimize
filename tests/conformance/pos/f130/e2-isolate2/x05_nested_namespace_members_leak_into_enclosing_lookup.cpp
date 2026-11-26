#include <cstdio>
namespace L { namespace v1 { int f(int) { return 1; } } int f(double) { return 2; } }
int main() { std::printf("%d\n", L::f(1)); return 0; }
