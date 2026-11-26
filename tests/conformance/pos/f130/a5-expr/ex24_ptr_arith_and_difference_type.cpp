#include <cstdio>
#include <cstddef>
#include <type_traits>
int main() { int a[10]; int *p = a + 2; int *q = &a[7]; std::ptrdiff_t d = q - p; unsigned long n = 2; int *r = q - n; std::printf("%d %d %d\n", (int)d, (int)(r - a), (int)std::is_same<decltype(q - p), std::ptrdiff_t>::value); return 0; }
