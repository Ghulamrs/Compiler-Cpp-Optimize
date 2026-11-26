// [temp.local]: the move constructor of Tu<P, int> names its own H, P, after a constructor template of its base
// Tu<int> was tried for the base's initialiser and its body did not compile.
#include <cstdio>
template <class T> struct rr { typedef T type; };
template <class T> struct rr<T &> { typedef T type; };
template <class T> struct rr<T &&> { typedef T type; };
template <class T> typename rr<T>::type &&mv(T &&t) { return static_cast<typename rr<T>::type &&>(t); }
template <class T> T &&fw(typename rr<T>::type &t) { return static_cast<T &&>(t); }
template <class T> T &&fw(typename rr<T>::type &&t) { return static_cast<T &&>(t); }
template <class... T> struct Tu;
template <> struct Tu<> { Tu() {} };
template <class H, class... T> struct Tu<H, T...> : Tu<T...> {
    typedef Tu<T...> tail_t;
    H h;
    tail_t &tail() { return *this; }
    const tail_t &tail() const { return *this; }
    template <class U, class... V> explicit Tu(U &&u, V &&... v) : tail_t(fw<V>(v)...), h(fw<U>(u)) {}
    Tu(const Tu &o) : tail_t(o.tail()), h(o.h) {}
    Tu(Tu &&o) : tail_t(mv(o.tail())), h(fw<H>(o.h)) {}
};
struct P { int n; };
template <class F, class... B> struct G {
    F f;
    Tu<B...> t;
    template <class... A> explicit G(const F &g, A &&... a) : f(g), t(fw<A>(a)...) {}
};
template <class F, class... B> G<F, B...> mk(F f, B... b) { return G<F, B...>(f, b...); }
int sq(int x) { return x * x; }
int main() {
    P p = {9};
    auto g = mk(sq, p, 4);
    auto k = g;
    std::printf("%d %d %d\n", k.t.h.n, k.t.tail().h, k.f(3));
    return 0;
}
