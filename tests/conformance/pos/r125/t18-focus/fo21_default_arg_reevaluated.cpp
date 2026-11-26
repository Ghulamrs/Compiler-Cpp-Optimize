// A default argument is evaluated at each call ([dcl.fct.default]/9).
#include <cstdio>
int counter = 0;
int next() { return ++counter; }
int f(int a = next()) { return a; }
struct S { int m(int a = next()) { return a; } };
int main() { int a = f(); int b = f(); S s; int c = s.m(); int d = f(10); std::printf("%d %d %d %d %d\n", a, b, c, d, counter); return 0; }
