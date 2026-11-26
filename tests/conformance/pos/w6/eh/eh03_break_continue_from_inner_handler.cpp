// [stmt.break], [stmt.cont] out of a handler nested in a handler: to a loop around both, and to a loop inside the outer handler
#include <cstdio>
struct T { int i; T(int i) : i(i) {} ~T() { std::printf("~%d ", i); } };
int main() {
    for (int i = 0; i < 4; i++) {
        try { throw i; }
        catch (int e) {
            T t(e);
            try { throw e; }
            catch (int k) { if (k == 1) continue; if (k == 3) break; }
            std::printf("n%d ", e);
        }
    }
    std::printf("| ");
    try { throw 9; }
    catch (int) {
        for (int j = 0; j < 3; j++) {
            try { throw j; }
            catch (int k) { if (k == 1) continue; std::printf("j%d ", k); if (k == 2) break; }
        }
        std::printf("out ");
    }
    std::printf("\n");
    return 0;
}
