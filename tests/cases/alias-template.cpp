// **An alias template** - [temp.alias], C++11: `template <class T> using Ptr = T *;` names a
// family of types, and `Ptr<int>` *is* `int *` - the type-id with the arguments substituted,
// not a type of its own. So it deduces through (`f(Ptr<T>)` takes an `int *` and works T out),
// it spells no name of its own in a signature, and it may name a class template, a non-type
// parameter, or another alias. It was refused by name - once as a C++14 variable template.
//
// Measured against clang -std=c++11 -pedantic-errors.
extern "C" int printf(const char *, ...);

template <class T> using Ptr = T *;
template <class T> using Ref = const T &;

template <class T, int N> struct Arr { T a[N]; int size() const { return N; } };
template <class T> using Three = Arr<T, 3>;
template <int N> using Ints = Arr<int, N>;
template <class T> using PtrPtr = Ptr<Ptr<T> >;

template <class T> T first(Ptr<T> p) { return *p; }      // T deduced through the alias
int total(Ref<Three<int> > t) { return t.a[0] + t.a[1] + t.a[2]; }
int count(Ints<5> &v) { return v.size(); }

int main() {
    int n = 7;
    Ptr<int> p = &n;
    PtrPtr<int> pp = &p;
    Three<int> t = { { 1, 2, 3 } };
    Ints<5> five;
    printf("%d %d %d\n", **pp, first(p), first(&n));
    printf("%d %d %d\n", total(t), count(five), (int)sizeof(Three<double>));
    return 0;
}
