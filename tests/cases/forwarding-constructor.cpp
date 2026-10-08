// **A constructor template taking a forwarding reference**, `template <class U> Box(U &&)`:
// an lvalue deduces U as `M &` and copies, an rvalue deduces U as M and moves. The rvalue
// call used to be ambiguous with itself - the specialization `Box<M>::Box<M>` keyed under
// the same name twice, once as a constructor and once as a specialization of the template.
//
// Measured against clang -std=c++11 -pedantic-errors. One call per statement.
extern "C" int printf(const char *, ...);

struct M {
    int v;
    M(int n);
    M(const M &o);
    M(M &&o);
};
M::M(int n) : v(n) {}
M::M(const M &o) : v(o.v + 100) {}
M::M(M &&o) : v(o.v + 200) { o.v = -1; }

template <class T> struct Box {
    T t;
    template <class U> Box(U &&u) : t(static_cast<U &&>(u)) {}
};

int main() {
    M m(1);
    Box<M> b1(m);
    printf("%d %d\n", b1.t.v, m.v);
    Box<M> b2(static_cast<M &&>(m));
    printf("%d %d\n", b2.t.v, m.v);
    const M cm(7);
    Box<M> b3(cm);
    printf("%d\n", b3.t.v);
    Box<M> b4(M(4));
    printf("%d\n", b4.t.v);
    return 0;
}
