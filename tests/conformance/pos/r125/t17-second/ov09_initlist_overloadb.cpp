// [over.match.list], [over.ics.list]: braced argument conversion sequences: to initializer_list<T>, to aggregate, to class with ctor, narrowing makes it non-viable, empty braces to default ctor, nested braces.
#include <cstdio>
#include <initializer_list>
#include <string>
struct Agg { int a, b; };
struct Cls { Cls(int x, double y) : s(x + (int)y) {} int s; };
const char *f(std::initializer_list<int>) { return "il<int>"; }
const char *f(Agg) { return "Agg"; }
const char *g(std::initializer_list<double>) { return "il<double>"; }
const char *g(int) { return "int"; }
const char *k(int) { return "int"; }
const char *k(std::string) { return "string"; }
const char *m(std::initializer_list<long>) { return "il<long>"; }
const char *m(std::initializer_list<int>) { return "il<int>"; }
int main() {
    std::printf("%s\n", m({1, 2}));
    return 0;
}
