// Default template args, dependent default args, friend templates, nested class templates, template instantiation of static locals (one per specialisation), templates with local types as args, explicit instantiation declaration ignored.
#include <cstdio>
template <class T, class U = T, int N = sizeof(T)> struct Trip { static int n() { return N; } U u; };
template <class T> struct Counter { static int next() { static int c = 0; return ++c; } };
template <class T> class Priv { T v; public: Priv(T v) : v(v) {} T peekv() const { return v; } };
template <class U> U peek(const Priv<U> &p) { return p.peekv(); }
template <class T> struct Outer { template <class U> struct Inner { T t; U u; int both() const { return (int)t + (int)u; } }; Inner<double> in; };
template <class T> int sizeOf() { return (int)sizeof(T); }
template <class T> T idf(T t) { return t; }
int main() {
    std::printf("%d %d %d\n", Trip<int>::n(), Trip<char, int>::n(), Trip<double, int, 3>::n());
    Trip<char> tc; tc.u = 'z'; std::printf("%c\n", tc.u);
    Counter<int>::next(); Counter<int>::next(); Counter<double>::next();
    std::printf("%d %d\n", Counter<int>::next(), Counter<double>::next());
    Priv<int> p(11); std::printf("%d\n", peek(p));
    Outer<int> o; o.in.t = 1; o.in.u = 2.5; std::printf("%d\n", o.in.both());
    struct Local { int a; short b; }; enum LocalEnum { LE = 4 };
    std::printf("%d %d\n", sizeOf<Local>(), idf(LE));
    std::printf("%d\n", sizeOf<int>());
    return 0;
}
