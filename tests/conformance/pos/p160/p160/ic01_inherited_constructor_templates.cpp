// [class.inhctor]: `using B::B` inherits constructor templates too - a specialization is made for the arguments,
// the derived class's own members initialised as usual.
#include <cstdio>
struct B { int v; template <class T> B(T t) : v(int(t)) {} B(int a, int b) : v(a + b) {} };
struct D : B { using B::B; int w = 7; };
struct F { int n; template <class... A> F(A &&... a) : n(sizeof...(a)) {} };
struct G : F { using F::F; };
template <class X> struct Q { X x; template <class U> explicit Q(U u) : x(u) {} };
template <class X> struct R : Q<X> { using Q<X>::Q; };
int main() {
    D d(2.5);
    D e('a');
    D f(1, 2);
    G g(1, 2, 3);
    R<long> r(3);
    std::printf("%d %d %d %d %d %ld\n", d.v, e.v, f.v, d.w, g.n, (long)r.x);
    return 0;
}
