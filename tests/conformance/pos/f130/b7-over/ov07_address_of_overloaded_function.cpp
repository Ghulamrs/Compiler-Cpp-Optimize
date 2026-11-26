#include <cstdio>
int f(int) { return 1; } int f(double) { return 2; } template <class T> int tf(T) { return 3; } struct S { int m(int) { return 4; } int m(double) { return 5; } static int s(int) { return 6; } static int s(char) { return 7; } };
int call(int (*p)(int)) { return p(1); } int calld(int (*p)(double)) { return p(1); }
int main() { int (*p1)(int) = f; int (*p2)(double) = &f; int (*p3)(char) = static_cast<int (*)(char)>(tf); int (S::*pm)(double) = &S::m; int (*ps)(char) = &S::s; S s; std::printf("%d %d %d %d %d %d %d %d\n", p1(1), p2(1), p3('c'), (s.*pm)(1), ps('c'), call(f), calld(f), call(tf)); return 0; }
