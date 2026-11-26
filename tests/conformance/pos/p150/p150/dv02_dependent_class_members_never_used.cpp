// [temp.res]/8 with [temp.inst]/1: members of class templates never used, valid as written - each statement
// that could fail names something depending on the parameters, a member of their type among them.
#include <cstdio>
template <class T> struct Holder {
    typedef T value_type;
    value_type x;
    T *p;
    int size() const { return (int)x.size(); }
    void call() { x.run(); p->stop(); }
    static int value() { return T::value; }
    template <class U> U as() const { return U(x); }
};
template <class T> struct Base { int m; };
template <class T> struct Derived : Base<T> { int get() const { return this->m; } };
template <class T, int N> struct Arr { T a[N]; int last() const { return a[N - 1].v; } };
template <class T> struct Copy { T t; Copy(const Copy &o) : t(o.t.clone()) {} Copy() {} };
int main() {
    Holder<int> h;
    h.x = 3;
    Derived<char> d;
    d.m = 4;
    std::printf("%d %d\n", h.x, d.get());
    return 0;
}
