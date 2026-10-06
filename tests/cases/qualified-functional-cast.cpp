// [expr.type.conv] through a qualified name: `S::T(3)` where T is a member
// typedef, `n::S::T(4)` in a namespace, `S::Nested(1, 2)` a nested class built
// as a temporary, and the same through a class template-id.
extern "C" int printf(const char *, ...);

struct S {
    typedef int T;
    typedef double D;
    struct Nested {
        int v;
        Nested(int a, int b);
    };
    typedef Nested Alias;
};
S::Nested::Nested(int a, int b) : v(a * 10 + b) {}

namespace n {
struct S {
    typedef long long T;
    struct In { int w; In(int x); };
};
}
n::S::In::In(int x) : w(x + 100) {}

template <class X> struct C { typedef X T; };
template <class X> struct B {
    struct Box { X held; Box(X h) : held(h) {} };
};

int main() {
    int a = S::T(3);
    double d = S::D(7) / 2;
    long long b = n::S::T(4);
    int c = S::Nested(1, 2).v;
    int al = S::Alias(3, 4).v;
    int w = n::S::In(5).w;
    int t = C<int>::T(5.9);
    double e = C<double>::T(2.5);
    int bx = B<int>::Box(6).held;
    int zero = S::T();
    printf("%d %g %lld %d %d %d %d %g %d %d\n", a, d, b, c, al, w, t, e, bx, zero);
    printf("%d %d\n", (int)sizeof(S::T(3)), (int)sizeof(n::S::T(3)));
    return 0;
}
