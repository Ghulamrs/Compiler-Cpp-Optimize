// [expr.prim.lambda]/19: a closure type's implicit copy and move constructors, reached through a template's member
// initialiser, with captures whose copies are constructors.
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>
template <class F> struct Hold {
    F f;
    explicit Hold(F g) : f(g) {}
    Hold(const Hold &o) : f(o.f) {}
    Hold(Hold &&o) : f(std::move(o.f)) {}
};
template <class F> Hold<F> mk(F f) { return Hold<F>(f); }
int main() {
    int n = 3;
    auto l = [n](int x) { return x + n; };
    auto c = l;
    auto m = std::move(l);
    auto h = mk(c);
    auto h2 = h;
    Hold<decltype(c)> h3(std::move(h2));
    std::printf("%d %d %d %d\n", c(1), m(2), h.f(3), h3.f(4));
    std::string s = "abc";
    std::vector<int> v(3, 1);
    auto big = [s, v](int x) { return (int)s.size() + (int)v.size() + x; };
    auto copied = big;
    auto moved = std::move(big);
    std::function<int(int)> fn = copied;
    auto nested = [copied](int y) { return copied(y); };
    auto again = nested;
    std::printf("%d %d %d %d\n", copied(1), moved(2), fn(3), again(4));
    return 0;
}
