// [temp.deduct.type]/8: `R (T::*)(P...)`, its const form and `R T::*` deduce from `&S::f` and `&S::v`.
#include <cstdio>
struct S {
    int v;
    int get() const { return v; }
    int add(int a) { return v + a; }
    int sum(int a, int b) { return v + a + b; }
};
template <class R, class T> int d(R T::*) { return 1; }
template <class R, class T, class... P> int f(R (T::*)(P...)) { return 10 + (int)sizeof...(P); }
template <class R, class T, class... P> int c(R (T::*)(P...) const) { return 20 + (int)sizeof...(P); }
template <class R, class T> int g(R (T::*)() const) { return 4; }
template <class R, class T, class A> R call(R (T::*pm)(A), T &t, A a) { return (t.*pm)(a); }
int main() {
    S s = {5};
    std::printf("%d %d %d %d %d %d\n", d(&S::v), f(&S::add), f(&S::sum), c(&S::get), g(&S::get), call(&S::add, s, 3));
    return 0;
}
