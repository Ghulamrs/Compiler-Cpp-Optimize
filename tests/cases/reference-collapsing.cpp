// **[dcl.ref]/6: a reference to a reference, reached through a typedef or a
// template parameter, collapses.** `& &`, `& &&` and `&& &` are all `&`; only
// `&& &&` is `&&`. It cannot be written directly - `int & &` is still a syntax
// error - which is why the four pairings here go through a typedef and through
// a template parameter bound to a reference.
//
// Measured against clang -std=c++11 -pedantic-errors.
extern "C" int printf(const char *, ...);

typedef int &LR;
typedef int &&RR;

int kind(int &)  { return 1; }
int kind(int &&) { return 2; }

// Each parameter type is the pairing named; what `kind` answers says what it collapsed to.
int lr_l(LR &x)   { return kind(static_cast<LR &>(x)); }     // & &   -> &
int lr_r(LR &&x)  { return kind(static_cast<LR &&>(x)); }    // & &&  -> &
int rr_l(RR &x)   { return kind(static_cast<RR &>(x)); }     // && &  -> &
int rr_r(RR &&x)  { return kind(static_cast<RR &&>(x)); }    // && && -> &&

// The same four through a template parameter bound to a reference type.
template <class T> int viaL(T &x)  { return kind(static_cast<T &>(x)); }
template <class T> int viaR(T &&x) { return kind(static_cast<T &&>(x)); }

// A local declared through the collapsed type refers to the caller's object.
template <class T> void set(T &&x, int v) {
    T &&alias = static_cast<T &&>(x);
    alias = v;
}

int main() {
    int n = 0;
    printf("%d %d %d %d\n", lr_l(n), lr_r(n), rr_l(n), rr_r(5));
    printf("%d %d %d %d\n", viaL<LR>(n), viaL<RR>(n), viaR<LR>(n), viaR<RR>(6));
    printf("%d %d\n", viaR<int &>(n), viaR<int &&>(7));

    set<int &>(n, 9);
    printf("%d\n", n);
    set(n, 11);
    printf("%d\n", n);
    return 0;
}
