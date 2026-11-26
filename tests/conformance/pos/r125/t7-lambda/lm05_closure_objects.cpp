// Closures as objects: copy of a closure with captured class with destructor (live count), closure stored and called later, closure with captured unique state, calling through a reference, closures in containers via function pointers.
#include <cstdio>
#include <vector>
struct Live { static int n; int v; Live(int v) : v(v) { ++n; } Live(const Live &o) : v(o.v) { ++n; } ~Live() { --n; } };
int Live::n = 0;
template <class F> int callTwice(F f) { return f() + f(); }
template <class F> int callRef(F &f) { return f(); }
int main() {
    {
        Live l(4);
        auto c = [l] { return l.v; };
        std::printf("%d ", Live::n);
        auto c2 = c;
        std::printf("%d %d %d ", Live::n, c(), c2());
        std::printf("%d\n", callTwice(c));
    }
    std::printf("%d\n", Live::n);
    int counter = 0; auto inc = [&counter] { return ++counter; };
    int ct = callTwice(inc); int cr = callRef(inc); std::printf("%d %d %d\n", ct, cr, counter);
    std::vector<int (*)(int)> fns; fns.push_back([](int x) { return x + 1; }); fns.push_back([](int x) { return x * 2; });
    int r = 3; for (int (*f)(int) : fns) r = f(r); std::printf("%d\n", r);
    return 0;
}
