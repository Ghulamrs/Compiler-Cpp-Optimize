// [dcl.type.simple]/4: decltype of a function's name is its type - the parameters adjusted - not the pointer the
// name decays to; of `&f` that pointer.
#include <cstdio>
#include <type_traits>
void f1(int a[10]);
void f2(int b[], int c(int));
void f3(const int d, int *const e);
int f4(double) { return 4; }
int main() {
    std::printf("%d", (int)std::is_same<decltype(f1), void(int *)>::value);
    std::printf("%d", (int)std::is_same<decltype(f2), void(int *, int (*)(int))>::value);
    std::printf("%d", (int)std::is_same<decltype(f3), void(int, int *)>::value);
    std::printf("%d", (int)std::is_same<decltype(&f4), int (*)(double)>::value);
    std::printf("%d", (int)std::is_function<decltype(f4)>::value);
    std::printf("%d\n", (int)std::is_same<decltype((f4)), int (&)(double)>::value);
    decltype(f4) *p = f4;
    std::printf("%d\n", p == &f4 ? 1 : 0);
    return 0;
}
