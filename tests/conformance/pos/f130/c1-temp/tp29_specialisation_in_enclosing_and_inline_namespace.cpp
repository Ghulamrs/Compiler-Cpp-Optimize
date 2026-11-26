#include <cstdio>
namespace N { template <class T> int f(T) { return 1; } template <class T> struct S { static const int v = 1; }; } template <> int N::f(int) { return 2; } template <> struct N::S<int> { static const int v = 2; }; namespace N { template <> int f(char) { return 3; } }
int main() { std::printf("%d %d %d %d %d\n", N::f(1), N::f(1.5), N::f('c'), N::S<int>::v, N::S<char>::v); return 0; }
