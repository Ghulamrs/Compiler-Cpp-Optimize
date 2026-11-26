// Member class templates of a class, of a class in a namespace and of a class template: their members, and functions taking them.
#include <cstdio>
struct S {
    template <class T> struct In { T v; T get() const { return v; } static int id() { return 7; } };
};
namespace N { struct P { template <class T, int K> struct Arr { T a[K]; int n() const { return K; } }; }; }
template <class T> struct W {
    template <class U> struct Pair { T t; U u; double sum() const { return t + u; } };
    Pair<int> make(T t) const { Pair<int> p; p.t = t; p.u = 2; return p; }
};
int use(const S::In<int> &x) { return x.get(); }
int useArr(const N::P::Arr<char, 3> &a) { return a.n(); }
double useW(W<double>::Pair<float> p) { return p.sum(); }
int main() {
    S::In<int> i; i.v = 4;
    N::P::Arr<char, 3> a;
    W<double> w;
    W<double>::Pair<float> p; p.t = 1.5; p.u = 2.25f;
    std::printf("%d %d %d %g %g\n", use(i), S::In<char>::id(), useArr(a), useW(p), w.make(1.0).sum());
    return 0;
}
