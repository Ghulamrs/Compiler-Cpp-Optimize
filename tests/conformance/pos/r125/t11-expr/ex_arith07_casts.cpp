// Casts: static_cast between numeric types and enums, through void*, base/derived; reinterpret_cast of pointers to integers and back, of function pointers; const_cast; C-style cast choosing among them; dynamic_cast on references.
#include <cstdio>
enum E { E1 = 1, E2 = 2 };
enum class F : char { X = 'x' };
struct B { int b; }; struct D : B { int d; };
int fn(int x) { return x + 1; }
int main() {
    std::printf("%d %d %c %d\n", static_cast<int>(E2), (int)static_cast<E>(1), (char)static_cast<F>('x'), static_cast<int>(2.9));
    D d; d.b = 1; d.d = 2; B *pb = &d; D *pd = static_cast<D *>(pb); std::printf("%d\n", pd->d);
    void *v = &d; D *pd2 = static_cast<D *>(v); std::printf("%d\n", pd2->b);
    unsigned long long addr = reinterpret_cast<unsigned long long>(&d); D *pd3 = reinterpret_cast<D *>(addr); std::printf("%d\n", pd3 == &d);
    int (*fp)(int) = fn; void *fv = reinterpret_cast<void *>(fp); int (*fp2)(int) = reinterpret_cast<int (*)(int)>(fv); std::printf("%d\n", fp2(1));
    const D cd = d; D *mut = const_cast<D *>(&cd); std::printf("%d\n", mut == &cd);
    std::printf("%d %d %d\n", (int)'A', (int)3.7f, (unsigned)E2);
    float fl = 1.5f; int bits = *reinterpret_cast<int *>(&fl); std::printf("%x\n", bits);
    long l = (long)-1; unsigned char uc = (unsigned char)l; std::printf("%d\n", uc);
    B &rb = d; D &rd = static_cast<D &>(rb); std::printf("%d\n", rd.d);
    std::printf("%d\n", (int)(bool)0.5);
    return 0;
}
