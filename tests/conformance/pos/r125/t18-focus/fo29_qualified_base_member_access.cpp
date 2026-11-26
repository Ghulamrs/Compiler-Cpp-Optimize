// A qualified name Base<T>::member inside a derived class template names the base subobject's non-static member.
#include <cstdio>
template <class T> struct Base { T v; Base() : v(T()) {} int f() const { return 10; } };
template <class T> struct Derived : Base<T> { void set(T x) { Base<T>::v = x; } T get() const { return Base<T>::v; } int g() const { return Base<T>::f() + 1; } };
struct PB { int v; }; struct PD : PB { void set(int x) { PB::v = x; } int get() const { return PB::v; } };
int main() { Derived<int> d; d.set(4); PD p; p.set(5); std::printf("%d %d %d\n", d.get(), d.g(), p.get()); return 0; }
