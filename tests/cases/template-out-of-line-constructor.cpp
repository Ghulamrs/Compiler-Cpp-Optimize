// **A constructor and a destructor of a class template written outside the class** -
// `template <class T> Box<T>::Box(...)`. From the `Box` after `::` the definition is shaped
// as a constructor held inside the class, so it is kept on the class template and replayed
// with T bound when the specialization is made, overload by overload as each is used.
// It was refused by name until 2026-10-08.
//
// Measured against clang -std=c++11 -pedantic-errors. One call with a side effect per statement.
extern "C" int printf(const char *, ...);

template <class T> struct Box {
    T v;
    int tag;
    Box();
    Box(T x, int k);
    Box(const Box &o);
    ~Box();
    T get() const;
};

template <class T> Box<T>::Box() : v(0), tag(1) { printf("Box() %d\n", tag); }
template <class T> Box<T>::Box(T x, int k) : v(x * k), tag(2) { printf("Box(T, int) %d\n", tag); }
template <class T> Box<T>::Box(const Box &o) : v(o.v + 1), tag(o.tag + 10) { printf("copy %d\n", tag); }
template <class T> Box<T>::~Box() { printf("~Box %d\n", tag); }
template <class T> T Box<T>::get() const { return v; }

// Defined after the use that asks for the class: the list is read again on every pass.
template <class T> struct Late {
    T n;
    explicit Late(T x);
};

int useLate();

int main() {
    Box<int> a;
    Box<double> d(1.5, 2);
    Box<int> c(a);
    printf("%d %g %d\n", a.get(), d.get(), c.get());
    int l = useLate();
    printf("%d\n", l);
    return 0;
}

int useLate() { Late<int> x(41); return x.n; }
template <class T> Late<T>::Late(T x) : n(x + 1) {}
