// [temp.over]/1: a candidate whose non-deduced parameter the argument cannot become is not viable, so what its body
// asks for is never made - std::true_type against false_type, at member and at namespace scope.
#include <cstdio>
#include <new>
#include <type_traits>
#include <utility>
template <class T, class... A> void build(long, T *p, A &&... a) { ::new (static_cast<void *>(p)) T(std::forward<A>(a)...); }
struct Sel {
    template <class T, class... A> void make(std::integral_constant<int, 0>, T *p, A &&... a) {
        build(0L, p, std::forward<A>(a)...);
    }
    template <class T, class... A> void make(std::integral_constant<int, 1>, T *p, A &&... a) {
        build(0L, p, 1, 2, 3, std::forward<A>(a)...);
    }
    template <class T, class... A> void go(T *p, A &&... a) {
        make(std::integral_constant<int, 0>(), p, std::forward<A>(a)...);
    }
};
template <class T> int pick(std::true_type, const T &t) { return t.size(); }
template <class T> int pick(std::false_type, const T &t) { return t + 1; }
int main() {
    int x = 0;
    Sel s;
    s.go(&x, 4);
    std::printf("%d %d\n", x, pick(std::false_type(), 2));
    return 0;
}
