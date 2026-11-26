// [dcl.fct]/2 with [dcl.stc]: a static member function declared 'static auto f() -> T', in a class and a class template.
#include <cstdio>
struct S { static auto twice(int x) -> int { return 2 * x; } static auto name() -> const char * { return "S"; } };
template <int I> struct G { static auto get(int t) -> decltype(G<I - 1>::get(t)) { return G<I - 1>::get(t) + 1; } };
template <> struct G<0> { static int get(int t) { return t; } };
template <class T> struct H { static auto make(T v) -> T { return v; } auto self() const -> const H * { return this; } };
int main() {
    H<double> h;
    std::printf("%d %s %d %g %d\n", S::twice(4), S::name(), G<3>::get(10), H<double>::make(1.5), (int)(h.self() == &h));
    return 0;
}
