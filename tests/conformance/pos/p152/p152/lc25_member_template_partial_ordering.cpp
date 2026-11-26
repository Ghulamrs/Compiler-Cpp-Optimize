#include <cstdio>
template <class... T> struct X {};
template <class A> int f(A, const X<int, char> &) { return 1; }
template <class A, class U, class... V> int f(A, const X<U, V...> &) { return 2; }
template <class... T> struct Y {
    template <class A> int g(A, const Y &) { return 1; }
    template <class A, class U, class... V> int g(A, const Y<U, V...> &) { return 2; }
};
int main() {
    X<int, char> x;
    Y<int, char> y;
    std::printf("%d %d\n", f(0, x), y.g(0, y));
}
