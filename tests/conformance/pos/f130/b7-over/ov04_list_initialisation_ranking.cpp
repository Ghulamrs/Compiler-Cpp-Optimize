#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>
int f(std::initializer_list<int>) { return 1; } int f(int) { return 2; } int g(std::initializer_list<double>) { return 1; } int g(std::initializer_list<int>) { return 2; } int h(std::vector<int>) { return 1; } int h(int) { return 2; } int k(std::string) { return 1; } int k(const char *) { return 2; }
struct S { S(std::initializer_list<int>) : v(1) {} S(int, int) : v(2) {} S() : v(3) {} int v; };
int main() { S a{1, 2}; S b(1, 2); S c{}; S d = {}; S e{{}}; std::printf("%d %d %d %d %d %d %d %d %d %d %d\n", f({1}), f({1, 2}), f(1), g({1, 2}), h({1, 2}), h(1), k({"s"}), k("s"), a.v, b.v, c.v); std::printf("%d %d\n", d.v, e.v); return 0; }
