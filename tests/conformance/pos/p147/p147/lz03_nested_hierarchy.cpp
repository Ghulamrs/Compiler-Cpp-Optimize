// Member classes of a specialization defined where first used: a base and a derived one, virtual functions, a static member.
#include <cstdio>
template <class T> struct Outer {
    struct Base { virtual int f() const { return 1; } virtual ~Base() {} };
    struct Derived : Base { int f() const { return (int)sizeof(T); } static int count; };
    struct Holder { Derived d; int call() const { const Base &b = d; return b.f() + Derived::count; } };
};
template <class T> int Outer<T>::Derived::count = 100;
int main() {
    Outer<double>::Holder h;
    Outer<short>::Derived s;
    const Outer<short>::Base &b = s;
    std::printf("%d %d\n", h.call(), b.f());
    return 0;
}
