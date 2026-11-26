// Array elements are destroyed in reverse order of construction; members and bases likewise.
#include <cstdio>
struct T { int v; T() : v(0) {} T(int v) : v(v) { std::printf("+%d ", v); } ~T() { std::printf("-%d ", v); } };
int main() {
    { T arr[3] = {T(1), T(2), T(3)}; std::printf("| "); } std::printf("\n");
    { T *p = new T[2]; p[0].v = 7; p[1].v = 8; delete[] p; } std::printf("\n");
    return 0;
}
