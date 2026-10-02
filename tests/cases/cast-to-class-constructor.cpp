// **A C-style cast or a static_cast to a class reaches its converting constructors**,
// [expr.cast]/4 through [expr.static.cast]/4: `(M)v` is the direct-initialisation `M t(v)`. It
// was refused as a cast between two unrelated classes with no conversion function - only that
// road had been built - and `(W)2.5` reinterpreted the bytes. Measured against clang.
extern "C" int printf(const char *, ...);
struct V { int x; V(int a) : x(a) {} };
struct M { int y; M(const V &v) : y(v.x * 10) {} };
struct W { int z; template <class T> W(const T &t) : z((int)t + 1) {} };
int main() {
    V v(4);
    M a = (M)v;
    M d = static_cast<M>(v);
    W b = (W)v.x;
    W c = (W)2.5;
    W e = static_cast<W>(7);
    printf("%d %d %d %d %d\n", a.y, d.y, b.z, c.z, e.z);
    return 0;
}
