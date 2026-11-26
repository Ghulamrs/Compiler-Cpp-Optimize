#include <cstdio>
template <class T> struct S { int f() { return 1; } static int s; struct In { int v = 1; }; template <class U> int g(U) { return 1; } }; template <class T> int S<T>::s = 1;
template <> int S<int>::f() { return 2; } template <> int S<int>::s = 2; template <> struct S<int>::In { int v = 2; }; template <> template <> int S<int>::g(char) { return 2; } template <> template <class U> int S<char>::g(U) { return 3; }
int main() { S<int> a; S<long> b; S<char> c; S<int>::In in; std::printf("%d %d %d %d %d %d %d %d\n", a.f(), b.f(), S<int>::s, S<long>::s, in.v, a.g('x'), a.g(1), c.g(1)); return 0; }
