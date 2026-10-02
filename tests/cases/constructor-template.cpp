// **A constructor that is itself a template**, [temp.mem] and [class.ctor]: declared in the class
// body, deduced from the argument - a type parameter, or a non-type one read out of a class
// template's argument list - ranked with the ordinary constructors, instantiated and emitted. Four
// ways in: direct-initialisation, a functional cast, a C-style cast, copy-initialisation.
// Measured against clang: the chosen constructor prints its own line, so the output is the choice.
extern "C" int printf(const char *, ...);
typedef decltype(sizeof(int)) size_t;   // clang-msvc and cxx1 both take this spelling

template <size_t N> struct V {
    double v[N];
    V() { for (size_t i = 0; i < N; i++) v[i] = 0; }
    V(double a, double b, double c) { v[0] = a; v[1] = b; v[2] = c; }
};

template <size_t M, size_t N> struct Mx {
    V<N> m[M];
    int how;
    Mx() : how(0) {}
    Mx(const V<N> &a, const V<N> &b, const V<N> &c) : how(1) { m[0] = a; m[1] = b; m[2] = c; }
    // Deduced from the class-template argument: K is 3 for a V<3>.
    template <size_t K> Mx(const V<K> &w) : how(2) {
        m[0] = V<N>(w.v[0], w.v[1], w.v[2]);
        m[1] = V<N>(-w.v[1], w.v[0], (double)K);
    }
    // Two parameters of one K, which also measures the Itanium substitution table.
    template <size_t K> Mx(const V<K> &a, const V<K> &b) : how(3) { m[0] = a; m[1] = b; }
};

struct S {
    int x;
    int how;
    S() : x(0), how(0) {}
    S(int a) : x(a), how(1) {}                            // the non-template wins an exact match
    template <class T> S(const T &t) : x((int)t * 2), how(2) {}
    template <class T> S(T *p, T q) : x(*p + q), how(3) {}
};

// A class whose only constructor is a template has no implicit default constructor to hide it.
struct Only {
    int k;
    template <class T> Only(T t) : k((int)t + 100) {}
};

static void show(const Mx<3, 3> &m) {
    printf("how %d: %g %g %g | %g %g %g\n", m.how, m.m[0].v[0], m.m[0].v[1], m.m[0].v[2],
           m.m[1].v[0], m.m[1].v[1], m.m[1].v[2]);
}

int main() {
    V<3> a(1, 2, 3);
    Mx<3, 3> m1(a);                     // direct-initialisation
    Mx<3, 3> m2 = Mx<3, 3>(a);          // functional cast
    Mx<3, 3> m3 = (Mx<3, 3>)a;          // C-style cast
    Mx<3, 3> m4 = a;                    // copy-initialisation
    Mx<3, 3> m5(a, a);                  // the two-parameter template
    Mx<3, 3> m6(a, a, a);               // the ordinary one still
    show(m1); show(m2); show(m3); show(m4); show(m5); show(m6);

    S s0;
    S s1(7), s2(2.5), s3 = 9, s4 = 1.5;
    int n = 20;
    S s5(&n, 3);
    S s6 = S(4.5);
    printf("%d/%d %d/%d %d/%d %d/%d %d/%d %d/%d %d/%d\n", s0.x, s0.how, s1.x, s1.how, s2.x, s2.how,
           s3.x, s3.how, s4.x, s4.how, s5.x, s5.how, s6.x, s6.how);
    Only o1(5), o2(2.5);
    printf("%d %d\n", o1.k, o2.k);
    return 0;
}
