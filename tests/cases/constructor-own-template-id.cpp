// A constructor and destructor named with the class's own template arguments,
// `V<N>(...)` and `~V<N>()` - the injected-class-name, [temp.local]/1. C++11
// takes it (C++20 does not); TI's cl6x 7.4.4 and 8.2.2 both compile it.
extern "C" int printf(const char *, ...);

template <int N> struct V {
    double a[N];
    V<N>(double *v = 0) { for (int i = 0; i < N; i++) a[i] = v ? v[i] : 0; }
    V<N>(double x, double y) { a[0] = x; a[1] = y; }
    explicit V<N>(int k) { for (int i = 0; i < N; i++) a[i] = k; }
    ~V<N>() { a[0] = -1; }
    V<N> twice() const { V<N> r; for (int i = 0; i < N; i++) r.a[i] = 2 * a[i]; return r; }
};

template <int M, int N> struct P {
    int m;
    P<M, N>() : m(M * 10 + N) { }
};

int main() {
    double d[2] = {1, 2};
    {
        V<2> p(d), q(3, 4), r;
        V<3> k(5);
        printf("%g %g %g %g %g %g\n", p.a[1], q.a[0], q.a[1], r.a[0], k.a[2], q.twice().a[1]);
    }
    P<2, 3> pm;
    printf("%d\n", pm.m);
    return 0;
}
