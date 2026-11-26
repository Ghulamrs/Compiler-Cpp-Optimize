// [temp.over]/1: a call's candidates are the specializations its own deduction makes. k<B>, made by the first
// call, is no candidate of k((D *)0, (B *)0), whose T is deduced D and B; the ellipsis answers.
#include <cstdio>
struct B {};
struct D : B {};
template <class T> int k(T *, T *) { return 1; }
int k(...) { return 2; }
template <class T> struct V { T v; };
template <class T> int operator+(V<T>, V<T>) { return 10; }
struct C {
    operator V<long>() const { V<long> r = {0}; return r; }
};
int operator+(C, V<int>) { return 20; }
int main() {
    B b;
    D d;
    int first = k(&b, &b);
    int second = k(&d, &b);
    V<long> a = {1}, e = {2};
    V<int> i = {3};
    C c;
    std::printf("%d %d %d %d\n", first, second, a + e, c + i);
    return 0;
}
