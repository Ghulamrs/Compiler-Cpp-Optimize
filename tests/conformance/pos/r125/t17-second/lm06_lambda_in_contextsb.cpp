// Lambdas in: default member initialisers, static locals, templates (instantiated per T), constructors' mem-initialisers, as template arguments to functions, inside another lambda's body referencing enclosing captures.
#include <cstdio>
template <class T> T twiceOf(T t) { auto l = [](T x) { return x + x; }; return l(t); }
struct S { int v; int w; S() { auto l = [] { return 21; }; v = l() * 2; auto m = [this] { return v + 1; }; w = m(); } };
template <class F> int apply3(F f) { return f(3); }
int main() {
    std::printf("%d %g\n", twiceOf(2), twiceOf(1.25));
    S s; std::printf("%d %d\n", s.v, s.w);
    static auto sl = [](int k) { return k * k; }; std::printf("%d\n", sl(5));
    std::printf("%d\n", apply3([](int k) { return k + 100; }));
    int base = 10; auto l = [&](int k) { auto in = [&](int m) { return base + k + m; }; return in(1); };
    std::printf("%d\n", l(2));
    auto immediate = [](int a, int b) { return a * b; }(6, 7); std::printf("%d\n", immediate);
    return 0;
}
