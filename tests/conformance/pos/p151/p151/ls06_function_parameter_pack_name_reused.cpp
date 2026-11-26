// [basic.scope.param]: a function parameter pack `a` is scoped to its function; a later function's ordinary
// parameter `a` is no pack, and `a[I]...` expands I alone.
#include <cstdio>
template <class... A> struct B {
    virtual int call(A &&... a) = 0;
    virtual ~B() {}
};
template <class... A> struct D : B<A...> {
    int call(A &&... a) { return (int)sizeof...(a); }
};
template <int... I> struct Ix {};
inline int sum() { return 0; }
template <class H, class... R> int sum(H h, R... r) { return h + sum(r...); }
template <class T, int... I> int pick(const T &a, Ix<I...>) { return sum(a[I]...); }
int main() {
    D<int, int> d;
    int v[3] = {1, 2, 3};
    std::printf("%d %d\n", d.call(1, 2), pick(v, Ix<0, 1, 2>()));
    return 0;
}
