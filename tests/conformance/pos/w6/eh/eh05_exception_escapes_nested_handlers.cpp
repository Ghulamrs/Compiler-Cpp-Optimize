// [except.ctor]/1: an exception thrown in an inner handler leaves both handlers; their locals are destroyed, innermost first
#include <cstdio>
struct T { int i; T(int i) : i(i) {} ~T() { std::printf("~%d ", i); } };
void f() {
    T a(1);
    try { throw 1; }
    catch (int) {
        T b(2);
        try { throw 2; }
        catch (int) { T c(3); throw 3.5; }
    }
}
int main() { try { f(); } catch (double d) { std::printf("caught %g\n", d); } return 0; }
