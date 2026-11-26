// Objects with destructors inside switch case blocks and loops with break/continue: every destructor runs once.
#include <cstdio>
struct T { static int n; T() { ++n; } ~T() { --n; } };
int T::n = 0;
int main() {
    for (int i = 0; i < 6; ++i) { T outer; switch (i % 3) { case 0: { T a; if (i == 3) break; continue; } case 1: { T b; break; } default: { T c; if (i == 5) goto out; } } }
out:
    std::printf("%d\n", T::n);
    return 0;
}
