// **A default template argument on a function template, on the deduced path** -
// [temp.param]/9 with [temp.deduct]: a parameter the call's arguments cannot work out
// takes its default, replayed with the earlier parameters bound, so `class U = T` reads
// what T was deduced as. The explicit-argument path has had this since the class case
// landed (template-default-argument.cpp); the deduced one left the parameter unbound and
// refused the call as having "nothing to work it out from".
//
// Measured against clang -std=c++11 -pedantic-errors.
extern "C" int printf(const char *, ...);

template <class T, class U = int> U narrow(T x) { return (U)x; }
template <class T, class U = T>   U same(T x)   { return x; }
template <class T, int N = 3>     T times(T x)  { return x * N; }
template <class T = int>          T zero()      { return T(); }
template <class T, class U = T *> int width(T x, U p = 0) { return sizeof(*p) + sizeof(x); }

// A default that is not used, because the call wrote the argument out.
template <class T, class U = double> U widen(T x) { return (U)x; }

struct Pt { int x, y; };

int main() {
    printf("%d %d\n", narrow(2.75), narrow<double, long long>(2.75) == 2);
    printf("%g %d\n", same(1.5), same(7));
    printf("%d %g %d\n", times(5), times(1.5), times<int, 10>(4));
    printf("%d %g\n", zero(), zero<double>());
    printf("%d %d\n", width(1), width('c'));
    printf("%g %d\n", widen(3), widen<int, char>(65));
    Pt p = { 2, 3 };
    Pt q = same(p);
    printf("%d %d\n", q.x, q.y);
    return 0;
}
