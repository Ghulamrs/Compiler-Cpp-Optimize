// [except.handle]/3: a handler of type T *& binds to the exception object itself; T *const & takes a derived pointer too
#include <cstdio>
struct B { int b; }; struct D : B { int d; };
int g; D dd;
int main() {
    try { try { throw &g; } catch (int *&p) { std::printf("%d ", p == &g); p = 0; throw; } }
    catch (int *q) { std::printf("%d ", q == 0); }
    try { throw &dd; } catch (B *const &p) { std::printf("%d ", p == &dd); }
    try { throw &g; } catch (int *const &p) { std::printf("%d\n", p == &g); }
    return 0;
}
