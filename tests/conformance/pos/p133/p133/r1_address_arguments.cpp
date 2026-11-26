#include <cstdio>
#include <cstddef>
int gv = 5, gv2 = 7;
int arr[3] = {1, 2, 3};
const char str[] = "hey";
namespace ns { int nv = 11; }
void fn() { std::printf("fn "); }
void ov(int) { std::printf("ov-int "); }
void ov(double) { std::printf("ov-double "); }
struct S {
    int a = 1, m = 3;
    static int sm;
    static void sf() { std::printf("sf "); }
    int f() { return 4; }
};
int S::sm = 21;
template <int *P> struct Box {
    static int count;
    int get() const { return *P; }
    void set(int v) { *P = v; }
};
template <int *P> int Box<P>::count = 0;
template <int *P> int deref() { return *P; }
template <int *P> int outer() { return deref<P>() + 100; }
template <void (*F)()> void call() { static int n = 0; n++; F(); std::printf("%d ", n); }
template <void (*F)(double)> void callo() { F(1.0); }
template <void (&F)()> void callr() { F(); }
template <const int &R> int rd() { return R; }
template <int (*PA)[3]> int third() { return (*PA)[2]; }
template <int *P = &gv> int dflt() { return *P; }
template <int S::*PM> int member(const S &s) { return s.*PM; }
template <std::nullptr_t N> bool isnull() { return sizeof(N) == sizeof(void *); }
template <int *P> bool nullp() { return P == nullptr; }
template <const char *C> char at(int i) { return C[i]; }
template <class T, T *P> T twice() { return *P * 2; }
int main() {
    call<fn>(); call<&fn>(); call<(&fn)>(); std::printf("\n");
    callo<ov>(); callr<fn>(); call<&S::sf>(); std::printf("\n");
    Box<&gv> a; Box<&gv2> b; Box<&gv> c;
    a.set(9); Box<&gv>::count++; c.get(); Box<&gv>::count++;
    std::printf("%d %d %d %d %d\n", a.get(), b.get(), c.get(), Box<&gv>::count, Box<&gv2>::count);
    std::printf("%d %d %d %d\n", deref<&ns::nv>(), outer<&gv2>(), deref<&S::sm>(), dflt<>());
    S s; s.m = 30;
    std::printf("%d %d %d %d\n", rd<gv>(), third<&arr>(), member<&S::m>(s), member<&S::a>(s));
    std::printf("%d %d %d %c%c\n", (int)isnull<nullptr>(), (int)nullp<nullptr>(), (int)nullp<&gv>(), at<str>(0), at<str>(2));
    std::printf("%d %ld\n", twice<int, &gv2>(), (long)sizeof(Box<&gv>));
    return 0;
}
