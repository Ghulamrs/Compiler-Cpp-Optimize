// [dcl.typedef]/2: alias-declarations for types, pointers, functions, arrays, member pointers; alias templates; alias in class scope; alias of alias.
#include <cstdio>
#include <vector>
using Int = int;
using IntPtr = int *;
using Fn = int (*)(int);
using Arr3 = int[3];
using FnRef = int (&)(int);
struct S { int m; int f(int x) const { return x + m; } using Self = S; };
using MemPtr = int S::*;
typedef int (S::*MemFn)(int) const;
template <class T> using Vec = std::vector<T>;
template <class T> using Ptr = T *;
template <class K, class V> struct Pair { K k; V v; };
template <class V> using IntPair = Pair<int, V>;
int dbl(int x) { return x * 2; }
template <class T> void show(Vec<T> &v) { std::printf("%d\n", (int)v.size()); }
int main() {
    Int i = 3; IntPtr p = &i; Fn f = dbl; Arr3 a = {1, 2, 3}; FnRef fr = dbl;
    std::printf("%d %d %d %d %d\n", i, *p, f(4), a[2], fr(5));
    S s = {10}; MemPtr mp = &S::m; MemFn mf = &S::f; S::Self s2 = {11};
    std::printf("%d %d %d\n", s.*mp, (s.*mf)(1), s2.m);
    Vec<int> v = {1, 2, 3}; Ptr<const char> str = "ok"; IntPair<double> ip = {1, 2.5};
    std::printf("%d %s %d %g\n", (int)v.size(), str, ip.k, ip.v);
    show(v);
    Vec<Vec<int> > vv(2, v); std::printf("%d\n", (int)vv[1].size());
    using Vec2 = Vec<Vec<int> >; Vec2 w = vv; std::printf("%d\n", (int)w.size());
    return 0;
}
