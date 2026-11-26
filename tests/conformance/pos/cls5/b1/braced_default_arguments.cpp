#include <cstdio>
#include <vector>
#include <string>
struct P { int a, b; };
struct C { int x, y; C(int x, int y) : x(x), y(y) {} };
int f(P p = {1, 2}) { return p.a * 10 + p.b; }
int g(int n, C c = {3, 4}) { return n + c.x * c.y; }
int h(std::vector<int> v = {1, 2, 3}) { int s = 0; for (int i : v) s += i; return s; }
std::size_t k(const std::string &s = {"abc"}) { return s.size(); }
int m(int i = {7}) { return i; }
struct S { int call(P p = {5, 6}) const { return p.a + p.b; } };
int main() {
    S s;
    std::printf("%d %d %d %d %d %d %d\n", f(), f({3, 4}), g(1), h(), (int)k(), m(), s.call());
    return 0;
}
