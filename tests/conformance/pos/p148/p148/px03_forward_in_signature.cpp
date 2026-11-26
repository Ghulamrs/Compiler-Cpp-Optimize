// [temp.variadic]/4 in a trailing return type: f(std::forward<A>(a)...) and f(a...) inside decltype.
#include <cstdio>
#include <utility>
template <class F, class... A> auto call(F f, A &&... a) -> decltype(f(std::forward<A>(a)...)) { return f(std::forward<A>(a)...); }
template <class F, class... A> auto byValue(F f, A... a) -> decltype(f(a...)) { return f(a...); }
template <class F> auto none(F f) -> decltype(f()) { return f(); }
int add3(int a, int b, int c) { return a + b + c; }
double half(double d) { return d / 2; }
const char *kind(int &) { return "lv"; }
const char *kind(int &&) { return "rv"; }
int seven() { return 7; }
int main() {
    int x = 1;
    std::printf("%d %g %d\n", call(add3, 1, 2, 3), call(half, 5.0), byValue(add3, 4, 5, 6));
    std::printf("%s %s %d\n", call(static_cast<const char *(*)(int &)>(kind), x),
                call(static_cast<const char *(*)(int &&)>(kind), 2), none(seven));
    return 0;
}
