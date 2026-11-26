// [except.handle]/15: a destructor's function-try handler sees what a member's destructor throws, then rethrows
#include <cstdio>
#include <new>
struct M { ~M() noexcept(false) { std::printf("~M "); throw 4; } };
struct S { M m; ~S() noexcept(false) try { std::printf("~S "); } catch (int e) { std::printf("handler%d ", e); } };
double raw[2];
int main() {
    S *p = new (raw) S;
    try { p->~S(); } catch (int e) { std::printf("outer%d\n", e); }
    return 0;
}
