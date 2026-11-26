// R16: a constexpr function template's specialization where a constant needs it.
#include <cstdio>
template <class T> constexpr T sq(T x) { return x * x; }
template <class T> constexpr int twice(T x) { return 2 * x; }
template <int N> struct Arr { int a[N]; };
int global[sq(3)];
extern "C" { int cside[twice(2)]; }
int main() {
    Arr<sq(2)> a;
    static_assert(sq(4) == 16, "sq");
    static_assert(twice(sq(2)) == 8, "twice");
    int local[twice(3)];
    std::printf("%d %d %d %d %d\n", (int)(sizeof(global) / sizeof(int)), (int)(sizeof(a) / sizeof(int)),
                (int)(sizeof(local) / sizeof(int)), (int)(sizeof(cside) / sizeof(int)), sq(5));
    return 0;
}