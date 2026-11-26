// Optimiser must respect aliasing: writes through char* and through pointers of the object's type, a pointer that aliases a loop bound, a global modified by a call inside a loop, a reference parameter that aliases another.
#include <cstdio>
#include <cstring>
int bound = 5; int g = 0;
void bumpG() { ++g; }
int loopWithCall() { int s = 0; for (int i = 0; i < 10; ++i) { s += g; bumpG(); } return s; }
int aliasBound(int *p) { int n = 0; for (int i = 0; i < bound; ++i) { ++n; if (i == 0) *p = 3; } return n; }
void addTo(int &a, const int &b) { a += b; a += b; }
int viaChar(int *v) { char *c = (char *)v; c[0] = 1; c[1] = 0; c[2] = 0; c[3] = 0; return *v; }
struct S { int a; int b; };
int memberAlias(S *s, int *p) { s->a = 1; *p = 2; return s->a; }
int main() {
    std::printf("%d %d\n", loopWithCall(), g);
    std::printf("%d\n", aliasBound(&bound));
    int x = 1; addTo(x, x); std::printf("%d\n", x);
    int v = 0x7FFFFFFF; std::printf("%d\n", viaChar(&v));
    S s = {0, 0}; std::printf("%d\n", memberAlias(&s, &s.a));
    int arr[4] = {1, 2, 3, 4}; std::memmove(arr + 1, arr, 3 * sizeof(int)); std::printf("%d %d %d %d\n", arr[0], arr[1], arr[2], arr[3]);
    int y = 5; int *py = &y; *py = 6; std::printf("%d\n", y);
    return 0;
}
