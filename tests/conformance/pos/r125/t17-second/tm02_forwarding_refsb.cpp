// [temp.deduct.call]/3: T&& with lvalue deduces T = U&, reference collapsing; std::forward preserves category; forwarding to overloads; const lvalues; forwarding through two layers.
#include <cstdio>
#include <utility>
template <class A, class B> struct Same { static const bool v = false; };
template <class A> struct Same<A, A> { static const bool v = true; };
const char *sink(int &) { return "lv"; }
const char *sink(const int &) { return "clv"; }
const char *sink(int &&) { return "rv"; }
template <class T> const char *fwd(T &&t) { return sink(std::forward<T>(t)); }
template <class T> const char *fwd2(T &&t) { return fwd(std::forward<T>(t)); }
template <class T> const char *nofwd(T &&t) { return sink(t); }
template <class T> bool isLref(T &&) { return Same<T, typename std::remove_reference<T>::type &>::v; }
template <class T> int deducedKind(T &&) { return std::is_lvalue_reference<T>::value ? 1 : (std::is_rvalue_reference<T>::value ? 2 : 0); }
int main() {
    int x = 1; const int cx = 2;
    std::printf("%s %s %s %s\n", fwd(x), fwd(cx), fwd(3), fwd(std::move(x)));
    std::printf("%s\n", fwd2(x));
    std::printf("%s %s\n", nofwd(x), nofwd(5));
    std::printf("%d %d %d\n", isLref(x), isLref(6), isLref(cx));
    std::printf("%d %d %d\n", deducedKind(x), deducedKind(7), deducedKind(std::move(x)));
    int &&r = 8; std::printf("%s %s\n", fwd(r), fwd(std::move(r)));
    return 0;
}
