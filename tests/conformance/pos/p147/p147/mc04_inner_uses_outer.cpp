// A member class template uses its class's parameters and static members, and is named bare inside the class.
#include <cstdio>
template <class T, int N> struct Outer {
    static int scale() { return N; }
    template <class U> struct Inner { U u; T t; int calc() const { return (int)(u * scale()) + (int)t; } };
    int run() const { Inner<double> in; in.u = 1.5; in.t = 2; return in.calc(); }
};
int main() {
    Outer<int, 4> o;
    Outer<short, 2>::Inner<int> x; x.u = 3; x.t = 1;
    std::printf("%d %d\n", o.run(), x.calc());
    return 0;
}
