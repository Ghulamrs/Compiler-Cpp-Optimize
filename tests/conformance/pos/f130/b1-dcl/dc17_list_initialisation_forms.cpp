#include <cstdio>
#include <vector>
#include <string>
#include <initializer_list>
struct Agg { int a; double b; const char *c; }; struct Ctor { int v; Ctor(int a, int b) : v(a * 10 + b) {} }; struct IL { int n; IL(std::initializer_list<int> l) : n((int)l.size()) {} IL(int, int) : n(-1) {} };
struct Def { int v = 9; Def() {} Def(std::initializer_list<int>) : v(-2) {} };
int main() { Agg a{1, 2.5, "x"}; Agg b = {3}; Agg c{}; Ctor d{1, 2}; Ctor e = {3, 4}; IL f{1, 2, 3}; IL g{1, 2}; IL h(1, 2); Def i{}; Def j{1}; int k{}; int *p{}; double m{2}; std::vector<int> v{1, 2, 3}; std::vector<std::string> vs{"a", "bc"}; int arr[2][2] = {1, 2, 3, 4}; char s[] = {"ab"}; Agg arr2[] = {{1, 1, "a"}, {2}};
std::printf("%d %g %s %d %g %d %d %d %d %d %d %d %d %d %p %g %d %d %d %d %d\n", a.a, a.b, a.c, b.a, c.b, (int)(c.c == nullptr), d.v, e.v, f.n, g.n, h.n, i.v, j.v, k, (void*)p, m, (int)v.size(), (int)vs[1].size(), arr[1][0], s[1], arr2[1].a); return 0; }
