// [temp.res]: typename for dependent types, template for dependent templates, dependent base members via this->, qualified names, name hiding in derived templates, ADL with templates.
#include <cstdio>
template <class T> struct Base { typedef T value_type; T v; Base() : v(T()) {} int baseFn() const { return 10; } template <class U> U cast() const { return (U)v; } };
template <class T> struct Derived : Base<T> {
    typedef typename Base<T>::value_type vt;
    vt get() const { return this->v; }
    int call() const { return this->baseFn() + Base<T>::baseFn(); }
    double asDouble() const { return this->template cast<double>(); }
    void set(vt x) { this->v = x; }
};
template <class C> typename C::value_type firstOf(const C &c) { return *c.begin(); }
struct Ints { typedef int value_type; int a[2]; const int *begin() const { return a; } };
namespace NS { struct Tag {}; template <class T> int adl(T, Tag) { return 7; } }
template <class T> int useAdl(T t) { return adl(t, NS::Tag()); }
template <class T> struct Outer { template <class U> struct Inner { U u; }; };
template <class T> int useInner() { typename Outer<T>::template Inner<int> in; in.u = 5; return in.u; }
int main() {
    Derived<int> d; d.set(4);
    std::printf("%d %d %g\n", d.get(), d.call(), d.asDouble());
    Ints is = {{8, 9}}; std::printf("%d\n", firstOf(is));
    std::printf("%d %d\n", useAdl(1), useInner<char>());
    return 0;
}
