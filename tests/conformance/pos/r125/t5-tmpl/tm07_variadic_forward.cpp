// Perfect forwarding of a pack to a constructor (make-like), to a function, with mixed categories; recursion with forwarding; pack in lambda capture-free call.
#include <cstdio>
#include <utility>
#include <string>
struct Obj { Obj(int a, const std::string &s, double d) : a(a), s(s), d(d) {} int a; std::string s; double d; };
template <class T, class... Args> T make(Args &&... args) { return T(std::forward<Args>(args)...); }
const char *cat(int &) { return "lv"; }
const char *cat(int &&) { return "rv"; }
const char *cat(const int &) { return "clv"; }
template <class... Ts> void show(Ts &&... ts) { const char *arr[] = {cat(std::forward<Ts>(ts))...}; for (const char *c : arr) std::printf("%s ", c); std::printf("\n"); }
template <class F, class... A> auto call(F f, A &&... a) -> decltype(f(std::forward<A>(a)...)) { return f(std::forward<A>(a)...); }
int add3(int a, int b, int c) { return a + b + c; }
template <class... Ts> int depth(Ts... ts) { return sizeof...(ts); }
template <class T, class... Ts> int nested(T, Ts... ts) { return depth(ts...) + depth(ts..., ts...); }
int main() {
    Obj o = make<Obj>(1, "str", 2.5); std::printf("%d %s %g\n", o.a, o.s.c_str(), o.d);
    int x = 1; const int cx = 2; show(x, cx, 3, std::move(x));
    std::printf("%d\n", call(add3, 1, 2, 3));
    std::printf("%d\n", nested(0, 1, 2, 3));
    std::string s = make<std::string>("abc"); std::printf("%s\n", s.c_str());
    return 0;
}
