// [temp.res]/8-10: templates never instantiated whose every questionable name depends on a parameter -
// valid as written, whatever their arguments would make of them.
#include <cstdio>
#include <algorithm>
#include <vector>
template <class T> void calls(T t) { g(t); t.foo(); T::bar(); typename T::type x; x.y = 1; }
template <class T> int asserts() { static_assert(sizeof(T) > 4, "wide"); return T::value; }
template <class T> void sorts(T &t) { std::sort(t.begin(), t.end()); }
template <class T> T twice(T t) { return t + t; }
template <int N> int arr() { int a[N - 1]; return (int)sizeof(a); }
template <class T> void fromT() { T x; int y = x; (void)y; }
template <class T> auto member(T t) -> decltype(t.x) { return t.x; }
template <class T, class U> U convert(const T &t) { U u(t.first, t.second); return u; }
template <class T> void lambdas(T t) { auto l = [&t](int k) { return t[k] + 1; }; l(0); }
template <class T> int locals(T t) { struct L { int v; } l = { (int)t.size() }; return l.v; }
template <class... Ts> int pack(Ts... ts) { return sum(ts...); }
template <class T> void ptr(T *p) { p->m(); delete p; }
int main() {
    std::vector<int> v(3, 1);
    std::printf("%d %d\n", twice(2), (int)v.size());
    return 0;
}
