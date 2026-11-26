#include <cstdio>
int g(int) { return 1; } template <class T> int call(T t) { return g(t) + g(1); } int g(double) { return 2; } struct S {}; int g(S) { return 3; }
template <class T> int adl(T t) { return h(t); } namespace N { struct X {}; int h(X) { return 7; } }
int main() { std::printf("%d %d %d %d\n", call(1.5), call(1), call(S()), adl(N::X())); return 0; }
