#include <cstdio>
#include <type_traits>
#include <string>
struct S { int v; }; S s1{1}, s2{2};
int main() { int a = 1, b = 2; bool c = true; (c ? a : b) = 9; const char *p = c ? "x" : "yy"; std::string s = c ? std::string("s") : "lit"; (c ? s1 : s2).v = 7;
std::printf("%d %d %s %s %d %d %d\n", a, b, p, s.c_str(), s1.v, (int)std::is_same<decltype(c ? a : b), int&>::value, (int)std::is_same<decltype(c ? 1 : 2.0), double>::value); return 0; }
