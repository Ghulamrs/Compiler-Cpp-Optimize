// **A pack of forwarding references, and a pattern expanded with `...`** - [temp.deduct.call]/3
// applied member by member, and [temp.variadic]/5: `kind(static_cast<A &&>(a))...` is one
// argument per member, each read with `a` naming that member and `A` its type. This is the
// shape `std::forward<A>(a)...` takes, and what an emplace-style constructor call needs.
// `const A &...` deduces each A without its const.
//
// Measured against clang -std=c++11 -pedantic-errors. One call with a side effect per statement.
extern "C" int printf(const char *, ...);

int kind(int &)       { return 1; }
int kind(const int &) { return 2; }
int kind(int &&)      { return 3; }

int three(int a, int b, int c) { return a * 100 + b * 10 + c; }
template <class... A> int each(A &&... a) { return three(kind(static_cast<A &&>(a))...); }

int none() { return 0; }
int chain() { return 0; }
template <class T, class... A> int chain(T &&t, A &&... a) {
    return kind(static_cast<T &&>(t)) * 10 + chain(static_cast<A &&>(a)...);
}


template <class... A> int count(const A &... a) { return (int)sizeof...(a) * 10 + (int)sizeof...(A); }

struct M {
    int v;
    M(int n);
    M(const M &o);
    M(M &&o);
};
M::M(int n) : v(n) {}
M::M(const M &o) : v(o.v + 100) {}
M::M(M &&o) : v(o.v + 200) {}

struct P {
    M a, b;
    P(const M &x, M &&y);
};
P::P(const M &x, M &&y) : a(x), b(static_cast<M &&>(y)) {}

// Building a P from what it was handed, in place: the emplace shape.
struct Slot {
    int n;
    Slot();
    template <class... A> int put(A &&... a) {
        P p(static_cast<A &&>(a)...);
        n = p.a.v * 1000 + p.b.v;
        return n;
    }
};

Slot::Slot() : n(0) {}

int main() {
    int i = 1;
    const int c = 2;
    int r1 = each(i, c, 3);
    int r2 = chain(c, i, 4);
    int r3 = count(i, 2.5, c, 'x');
    printf("%d %d %d\n", r1, r2, r3);
    M m(1);
    Slot s;
    int r4 = s.put(m, M(2));
    printf("%d %d\n", r4, none());
    return 0;
}
