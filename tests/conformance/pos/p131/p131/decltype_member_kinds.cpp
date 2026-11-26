#include <cstdio>
#include <type_traits>
int g = 3; struct S { int m; int &r; const int c; mutable int u; S() : m(1), r(g), c(2), u(4) {} };
int main() { const S cs; std::printf("%d %d %d %d %d\n", (int)std::is_same<decltype(cs.r), int &>::value, (int)std::is_same<decltype(cs.c), const int>::value, (int)std::is_same<decltype(cs.u), int>::value, (int)std::is_same<decltype((cs.m)), const int &>::value, (int)std::is_same<decltype((cs.u)), int &>::value); return 0; }
