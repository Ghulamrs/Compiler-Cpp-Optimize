// The other translation unit of extern-template: it instantiates the same specializations, so each
// is defined in both objects and the linker keeps one of each.
template <class T> struct S { T v; T twice() const { return v * 2; } };
template <class T> T add(T a, T b) { return a + b; }

int other(int n) {
    S<int> s;
    s.v = n;
    return add(s.twice(), 1);
}
