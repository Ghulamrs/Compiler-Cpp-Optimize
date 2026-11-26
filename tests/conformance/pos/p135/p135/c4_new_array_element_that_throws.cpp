// R28 with [except.ctor]/2: the elements built before a throwing one are destroyed, last first.
#include <cstdio>
#include <new>
static int built = 0;
struct T { int v; T(int x) : v(x) { if (x == 3) throw 3; built++; } ~T() { std::printf("~%d ", v); } };
int main() {
    try { T *t = new T[4]{1, 2, 3, 4}; (void)t; } catch (int e) { std::printf("caught %d after %d\n", e, built); }
    return 0;
}