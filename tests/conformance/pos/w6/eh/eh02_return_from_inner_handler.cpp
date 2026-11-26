// [stmt.return]: a return out of a handler nested in a handler destroys every local, innermost first
#include <cstdio>
struct T { int i; T(int i) : i(i) {} ~T() { std::printf("~%d ", i); } };
int f(int x) {
    T a(1);
    try { throw x; }
    catch (int e) {
        T b(2);
        try { throw e * 10; }
        catch (int k) { T c(3); if (k == 70) return k + e; }
        std::printf("fell ");
    }
    return -1;
}
struct V { int i; };
V g() {
    try { throw 5; }
    catch (int e) { try { throw e; } catch (int k) { V v; v.i = k * 2; return v; } }
    V z; z.i = 0; return z;
}
int main() {
    int r = f(7); std::printf("r%d ", r);
    r = f(1); std::printf("r%d ", r);
    std::printf("g%d\n", g().i);
    return 0;
}
