#include <cstdio>
volatile int g = 4;
const volatile int cg = 9;
typedef volatile int vint;
struct R { volatile int status; int plain; };
int f(int *) { return 1; }
int f(volatile int *) { return 2; }
int f(const volatile int *) { return 3; }
int byref(volatile int &r) { r += 10; return r; }
template <class T> int kind(T *) { return 0; }
template <class T> int kind(volatile T *) { return 1; }
template <class T> struct Is { static const int v = 0; };
template <class T> struct Is<volatile T> { static const int v = 1; };
template <class T> struct Is<const volatile T> { static const int v = 2; };
int main() {
    volatile int v = 1;
    v = v + 1; v += 3; ++v; v++;
    int x = v;
    volatile int *pv = &v;
    int *volatile vp = &x;
    const volatile int *cp = pv;
    vint w = 7;
    R r; r.status = 5; r.plain = 6;
    volatile R vr; vr.status = 8;
    std::printf("%d %d %d %d %d\n", x, *pv, *vp, *cp, w);
    std::printf("%d %d %d %d\n", f(&x), f(&v), f(cp), f(&g));
    std::printf("%d %d %d %d\n", byref(v), byref(g), r.status + r.plain, vr.status);
    std::printf("%d %d %d %d %d\n", kind(&x), kind(&v), Is<int>::v, Is<volatile int>::v, Is<const volatile int>::v);
    int *back = const_cast<int *>(pv);
    *back = 40;
    volatile int *again = static_cast<volatile int *>(&x);
    std::printf("%d %d %d %d\n", v, *again, cg, (int)sizeof(volatile double));
    int n = 0;
    volatile int countdown = 3;
    while (countdown) { --countdown; n++; }
    std::printf("%d %d\n", n, g);
    return 0;
}
