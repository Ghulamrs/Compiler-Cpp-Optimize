// [except.handle]: a try inside a handler inside a handler; each level catches its own throw
#include <cstdio>
int main() {
    try { throw 1; }
    catch (int a) {
        try { throw a + 1; }
        catch (int b) {
            try { throw b + 1; }
            catch (int c) { std::printf("%d %d %d ", a, b, c); }
            std::printf("b%d ", b);
        }
        std::printf("a%d\n", a);
    }
    return 0;
}
