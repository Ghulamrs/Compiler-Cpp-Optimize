// **The SampleExt shape**: a member template of one class template builds another class
// template through *its* constructor template, by a C-style cast - `(Mx<K, K>)lhs * rhs`. The
// specialization is instantiated inside the member's body, which is itself being replayed, so
// two bindings are in force and a third is made. Measured against clang.
extern "C" int printf(const char *, ...);
typedef decltype(sizeof(int)) size_t;   // clang-msvc and cxx1 both take this spelling

template <size_t M, size_t N> struct Mx;

template <size_t N> struct V {
    double v[N];
    V() { for (size_t i = 0; i < N; i++) v[i] = 0; }
    V(double a, double b, double c) { v[0] = a; v[1] = b; v[2] = c; }
    // The cross product, written as a skew matrix times the right operand.
    template <size_t K> V<K> operator*(const V<K> &rhs) const {
        V<N> lhs(*this);
        return (Mx<K, K>)lhs * rhs;
    }
};

template <size_t M, size_t N> struct Mx {
    V<N> m[M];
    Mx() {}
    template <size_t K> Mx(const V<K> &w) {
        m[0] = V<N>(0, -w.v[2], w.v[1]);
        m[1] = V<N>(w.v[2], 0, -w.v[0]);
        m[2] = V<N>(-w.v[1], w.v[0], 0);
    }
    template <size_t K> V<M> operator*(const V<K> &rhs) const {
        V<M> r;
        for (size_t i = 0; i < M; i++) {
            double s = 0;
            for (size_t k = 0; k < N; k++) s += m[i].v[k] * rhs.v[k];
            r.v[i] = s;
        }
        return r;
    }
};

int main() {
    V<3> a(1, 2, 3), b(4, 5, 6);
    V<3> c = a * b;
    printf("%g %g %g\n", c.v[0], c.v[1], c.v[2]);
    Mx<3, 3> s(b);
    V<3> d = s * a;
    printf("%g %g %g\n", d.v[0], d.v[1], d.v[2]);
    return 0;
}
