#include <cstdio>
template <class T> struct Base { virtual T get() const = 0; virtual ~Base() {} T twice() const { return get() * 2; } }; template <class T> struct Impl : Base<T> { T v; Impl(T v) : v(v) {} T get() const override { return v; } };
template <class T> struct Visitor { virtual void visit(T) = 0; }; struct Printer : Visitor<int>, Visitor<double> { void visit(int x) override { std::printf("i%d ", x); } void visit(double x) override { std::printf("d%g ", x); } };
int main() { Impl<int> i(3); Base<int> &b = i; std::printf("%d ", b.twice()); Printer p; p.visit(1); p.visit(2.5); std::printf("\n"); return 0; }
