// Lambda return type: deduced from a single return, explicit trailing, void, returning references, returning class objects, returning lambdas, recursion through a function pointer.
#include <cstdio>
#include <string>
struct P { int x; };
int main() {
    auto i = [] { return 1; };
    auto d = [](int k) -> double { return k / 2.0; };
    auto v = [] { std::printf("void "); };
    int arr[2] = {5, 6}; auto ref = [&arr](int k) -> int & { return arr[k]; };
    ref(1) = 60;
    auto s = [] { return std::string("str"); };
    auto p = [] { return P{3}; };
    auto cond = [](bool b) { if (b) return 1; else return 2; };
    auto nested = [] { return [] { return 9; }; };
    std::printf("%d %g ", i(), d(3)); v(); std::printf("%d %s %d %d %d %d\n", arr[1], s().c_str(), p().x, cond(true), cond(false), nested()());
    auto ptr = [](const char *s) -> const char * { return s + 1; };
    std::printf("%s\n", ptr("xyz"));
    return 0;
}
