// [except.throw]/8: a rethrow throws the exception of the innermost handler being executed, from any depth
#include <cstdio>
void rethrow() { throw; }
int main() {
    try { throw 1; }
    catch (int a) {
        try { try { throw 2; } catch (int) { throw; } }
        catch (int b) { std::printf("b%d ", b); }
        try { rethrow(); }
        catch (int c) { std::printf("c%d ", c); }
        std::printf("a%d\n", a);
    }
    return 0;
}
