// [dcl.ambig.res]/2 and [expr.sizeof]/1: `sizeof(S::m)` holds a type-id only if one can be
// read there, and S::m naming a data member is an expression. [expr.prim.general]/13 lets an
// unevaluated operand name a non-static member with no object. The types beside them stay types.
extern "C" int printf(const char *, ...);

struct P { int a; double b; };
struct S {
    static int m; static char arr[7]; static P p;
    int x; double d; char buf[11];
    struct Nested { char c[3]; };
    typedef short Typedef;
    int f() { return (int)sizeof(S::buf) + (int)sizeof(x); }
    static int g() { return (int)sizeof(S::buf) + (int)sizeof(buf); }
};
int S::m = 40; char S::arr[7]; P S::p;
struct Outer { struct Inner { static double m; int y; }; };
double Outer::Inner::m;
namespace n { struct S { static short m; int q[5]; }; }
short n::S::m;
template <class T> struct C { static T m; T x[2]; };
template <class T> T C<T>::m;
struct D : S { int y; int h() { return (int)sizeof(S::x) + (int)sizeof(D::y); } };

int main() {
    // A static data member of each kind, then a non-static one.
    printf("%d %d %d\n", (int)sizeof(S::m), (int)sizeof(S::arr), (int)sizeof(S::p));
    printf("%d %d %d\n", (int)sizeof(S::x), (int)sizeof(S::d), (int)sizeof(S::buf));
    printf("%d %d\n", (int)sizeof(Outer::Inner::m), (int)sizeof(Outer::Inner::y));
    printf("%d %d\n", (int)sizeof(n::S::m), (int)sizeof(n::S::q));
    printf("%d %d %d\n", (int)sizeof(C<int>::m), (int)sizeof(C<double>::m),
           (int)sizeof(C<double>::x));
    printf("%d %d %d\n", (int)sizeof(S::Nested), (int)sizeof(S::Typedef), (int)sizeof(n::S));
    printf("%d %d %d\n", (int)(sizeof(S::Nested *) == sizeof(void *)), (int)sizeof((S::m)), (int)sizeof S::m);
    printf("%d %d\n", (int)sizeof(S::buf[0]), (int)sizeof(S::arr + 1) == (int)sizeof(char *));

    // decltype of a qualified id-expression is the declared type; parenthesised, an lvalue.
    decltype(S::m) dm = 3; decltype(S::x) dx = 4; decltype(n::S::m) ds = 5;
    decltype(C<int>::m) cm = 6; decltype(C<char>::x) cx;
    n::S ns; ns.q[0] = 1;
    decltype((S::x)) r = ns.q[0]; r = 9;
    printf("%d %d %d %d %d %d %d\n", dm, dx, ds, cm, (int)sizeof(cx),
           (int)sizeof(decltype(S::arr)), ns.q[0]);

    // Inside the class, a derived class and a static member function.
    S s;
    D dd;
    printf("%d %d %d\n", s.f(), S::g(), dd.h());
    // Odr-used, so both compilers define all three specializations of C<T>::m.
    printf("%d\n", (int)((void *)&C<int>::m != (void *)&S::m) +
                   (int)((void *)&C<double>::m != (void *)&S::m) +
                   (int)((void *)&C<char>::m != (void *)&S::m));

    // The same parenthesis in a cast: an expression where it is one, a type where it is one.
    int a = (S::m) + 1;
    S::Typedef t = (S::Typedef)70000;
    int b = (int)(S::m) + (S::Typedef)3;
    n::S *pp = (n::S *)&ns;
    printf("%d %d %d %d\n", a, (int)t, b, (int)(pp == &ns));
    return 0;
}
