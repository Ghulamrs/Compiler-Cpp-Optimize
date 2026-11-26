#include <cstdio>
#include <cstddef>
template <std::nullptr_t N> int nv() { return 1; } int gv = 5; template <int *P> int deref() { return *P; } template <const char *C> int first() { return C[0]; } extern const char str[]; const char str[] = "s";
int main() { std::printf("%d %d %d\n", nv<nullptr>(), deref<&gv>(), first<str>()); return 0; }
