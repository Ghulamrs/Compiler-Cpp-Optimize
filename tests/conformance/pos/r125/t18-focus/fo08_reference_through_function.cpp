// A temporary passed to a function returning a reference is destroyed at the end of the full-expression, not at the end of the scope ([class.temporary]/5, second bullet).
#include <cstdio>
struct T { int v; T(int v) : v(v) { std::printf("+%d ", v); } ~T() { std::printf("-%d ", v); } };
const T &same(const T &t) { return t; }
int main() {
    { const T &r = same(T(1)); std::printf("| in-scope "); (void)r; } std::printf("| end\n");
    { int n = same(T(2)).v; std::printf("| in-scope %d ", n); } std::printf("| end\n");
    return 0;
}
