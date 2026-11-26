// Static members (definition, const integral in-class, constexpr), nested classes accessing enclosing privates (C++11 rule), local classes, local class as template argument (C++11), member enum/typedef.
#include <cstdio>
struct Outer {
    static int count; static const int K = 3; static constexpr double D = 1.5;
    typedef int Int; enum { EN = 7 };
    struct Nested { int get(const Outer &o) const { return o.secret; } static int s() { return K * 2; } };
    Outer() : secret(42) { ++count; }
private: int secret;
};
int Outer::count = 0;
constexpr double Outer::D;
const int Outer::K;
template <class T> int sizeOf() { return (int)sizeof(T); }
template <class T> T twice(T t) { return t.dbl(); }
int main() {
    Outer o1, o2; Outer::Nested n;
    std::printf("%d %d %g %d %d %d\n", Outer::count, Outer::K, Outer::D, n.get(o1), Outer::Nested::s(), Outer::EN);
    Outer::Int i = 5; std::printf("%d\n", i);
    struct Local { int v; Local dbl() const { Local l = {v * 2}; return l; } };
    Local l = {21}; std::printf("%d %d\n", twice(l).v, sizeOf<Local>());
    const int *pk = &Outer::K; std::printf("%d\n", *pk);
    struct Counter { static int next() { static int n = 0; return ++n; } };
    int n1 = Counter::next(); int n2 = Counter::next(); std::printf("%d %d\n", n1, n2);
    return 0;
}
