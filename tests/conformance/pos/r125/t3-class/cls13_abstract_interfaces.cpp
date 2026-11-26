// Multiple interface inheritance with virtual calls through each base pointer, dominance with virtual bases, calling the final overrider, sizeof of an interface pointer adjust.
#include <cstdio>
struct I1 { virtual int one() = 0; virtual ~I1() {} };
struct I2 { virtual int two() = 0; virtual ~I2() {} };
struct Impl : I1, I2 { int one() override { return 1; } int two() override { return 2; } };
struct VB { virtual int who() { return 0; } };
struct L : virtual VB { int who() override { return 1; } };
struct R : virtual VB {};
struct Dom : L, R {};
int viaI1(I1 *p) { return p->one(); }
int viaI2(I2 *p) { return p->two(); }
int main() {
    Impl im; I1 *a = &im; I2 *b = &im;
    std::printf("%d %d %d\n", viaI1(a), viaI2(b), (void *)a != (void *)b);
    I2 *heap = new Impl; std::printf("%d\n", heap->two()); delete heap;
    Dom d; VB *v = &d; R *r = &d;
    std::printf("%d %d %d\n", v->who(), r->who(), d.who());
    return 0;
}
