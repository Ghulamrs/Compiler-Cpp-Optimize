#include <cstdio>
template <class T> struct S { int f() { return 1; } }; template <> int S<int>::f() { return 2; }
int main() { S<int> a; S<long> b; std::printf("%d %d\n", a.f(), b.f()); return 0; }
