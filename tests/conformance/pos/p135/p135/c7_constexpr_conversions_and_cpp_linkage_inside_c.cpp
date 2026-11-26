// A constexpr conversion function, and [dcl.link]/4: extern "C++" inside extern "C".
#include <cstdio>
template <class T, T v> struct IC { static constexpr T value = v; constexpr operator T() const { return v; } };
struct Two { constexpr operator int() const { return 2; } };
extern "C" {
    int cfun(int x) { return x + 1; }
    extern "C++" {
        template <class T> T ident(T t) { return t; }
        int over(int) { return 1; }
        int over(double) { return 2; }
    }
}
int main() {
    int a[IC<int, 3>::value];
    std::printf("%d %d %d %d %d %d\n", (int)IC<int, 7>() + (int)Two(), (int)(sizeof(a) / sizeof(int)), cfun(1), ident(4), over(1), over(1.0));
    return 0;
}