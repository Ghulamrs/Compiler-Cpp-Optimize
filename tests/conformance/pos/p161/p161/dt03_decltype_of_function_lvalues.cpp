// [dcl.type.simple]/4: `(f)`, `*pf` and `*f` are lvalues of function type, so a reference to it; `&f` and a pointer
// variable are the pointer they are.
#include <cstdio>
#include <type_traits>
int f4(double) { return 4; }
int main() {
    int (*pf)(double) = f4;
    std::printf("%d", (int)std::is_same<decltype(*pf), int (&)(double)>::value);
    std::printf("%d", (int)std::is_same<decltype((f4)), int (&)(double)>::value);
    std::printf("%d", (int)std::is_same<decltype(&f4), int (*)(double)>::value);
    std::printf("%d", (int)std::is_same<decltype(pf), int (*)(double)>::value);
    std::printf("%d", (int)std::is_same<decltype((pf)), int (*&)(double)>::value);
    std::printf("%d\n", (int)std::is_same<decltype(*f4), int (&)(double)>::value);
    return 0;
}
