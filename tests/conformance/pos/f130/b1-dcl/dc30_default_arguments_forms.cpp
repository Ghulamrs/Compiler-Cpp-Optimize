#include <cstdio>
int gv = 10; int f(int a, int b = 2, int c = gv); int f(int a, int b, int c) { return a * 100 + b * 10 + c; } void h(int a, int b = 5); void h(int a, int b) { std::printf("%d ", a + b); }
struct S { int m(int x = 7) const { return x; } static int s(int x = 8) { return x; } S(int v = 3) : v(v) {} int v; };
template <class T = int, int N = 4> struct W { T t[N]; }; template <class T> int tf(T a, T b = T()) { return (int)(a + b); }
int main() { gv = 1; h(1); S s; W<> w; std::printf("%d %d %d %d %d %d %d %d\n", f(1), f(1, 2, 3), s.m(), S::s(), s.v, (int)sizeof(w) / 4, tf(2), tf(1.5, 2.5)); return 0; }
