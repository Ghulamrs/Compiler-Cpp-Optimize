// [stmt.switch] inside a handler: an inner handler's break and return through it, a value from both caught objects
#include <cstdio>
int f(int x) {
    try { throw x; }
    catch (int e) {
        switch (e) {
        case 1:
            try { throw e + 1; } catch (int k) { return e * 100 + k; }
        case 2:
            try { throw e + 2; } catch (int k) { if (k == 4) break; }
            return -2;
        default:
            break;
        }
        return e;
    }
    return 0;
}
int main() { std::printf("%d %d %d\n", f(1), f(2), f(3)); return 0; }
