// **A forwarding reference, [temp.deduct.call]/3.** `template <class T> void f(T &&)`
// called with an lvalue of type U deduces T as `U &`, and [dcl.ref]/6 collapses the
// parameter `U & &&` to `U &`; a const lvalue gives `const U &`; an rvalue gives T
// as U and the parameter stays `U &&`. `std::forward<T>(x)` then hands the argument
// on with the value category it arrived with, which is what lets one template reach
// the by-value, by-reference and by-rvalue-reference overloads of what it calls.
//
// Measured against clang -std=c++11 -pedantic-errors. Every call is its own
// statement, since the order of a call's arguments is unspecified.
#include <utility>
extern "C" int printf(const char *, ...);

struct S { int v; };

int sink(int &)        { return 1; }
int sink(const int &)  { return 2; }
int sink(int &&)       { return 3; }

template <class T> int plain(T &&x) { return sink(x); }                    // a named parameter is an lvalue
template <class T> int fwd(T &&x)   { return sink(std::forward<T>(x)); }   // forwarded as it came

int cat(S &)       { return 10; }
int cat(const S &) { return 20; }
int cat(S &&)      { return 30; }
template <class T> int fwdS(T &&x) { return cat(std::forward<T>(x)); }

// Two levels: the outer forwards into the inner, which forwards into the sink.
template <class T> int inner(T &&x) { return sink(std::forward<T>(x)); }
template <class T> int outer(T &&x) { return inner(std::forward<T>(x)); }

// By value: a forwarded lvalue copies and a forwarded rvalue moves.
struct Tracked {
    int v;
    Tracked(int n) : v(n) {}
    Tracked(const Tracked &o) : v(o.v + 100) {}
    Tracked(Tracked &&o) : v(o.v + 200) {}
};
int take(Tracked t) { return t.v; }
template <class T> int fwdT(T &&x) { return take(std::forward<T>(x)); }

// What T is, read back through the parameter's own type.
template <class T> int width(T &&x) { return sizeof(x); }

// And the collapsed parameter is a reference to the caller's object.
template <class T> void bump(T &&x) { ++x; }

int main() {
    int i = 5;
    const int ci = 6;

    printf("%d %d %d\n", plain(i), plain(ci), plain(7));
    printf("%d %d %d\n", fwd(i), fwd(ci), fwd(7));
    printf("%d\n", fwd(std::move(i)));

    S s = { 1 };
    const S cs = { 2 };
    printf("%d %d %d %d\n", fwdS(s), fwdS(cs), fwdS(S()), fwdS(std::move(s)));

    printf("%d %d %d\n", outer(i), outer(ci), outer(8));

    Tracked t(1);
    int a = fwdT(t);
    int b = fwdT(Tracked(2));
    int c = fwdT(std::move(t));
    printf("%d %d %d\n", a, b, c);

    char ch = 'x';
    double d = 2.5;
    printf("%d %d %d\n", width(ch), width(d), width(3.5f));

    // The forwarding reference still names the caller's object.
    int n = 1;
    bump(n);
    int &r = n;
    bump(r);
    printf("%d\n", n);
    return 0;
}
