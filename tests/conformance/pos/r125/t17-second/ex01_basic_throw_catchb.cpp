// [except]: throw by value, catch by value/reference/base/pointer/..., ordering of handlers, rethrow, nested try, exception leaves a loop, int and class types, catch const T&.
#include <cstdio>
struct Base { virtual ~Base() {} virtual const char *w() const { return "Base"; } };
struct Der : Base { const char *w() const override { return "Der"; } };
int classify(int k) {
    try {
        if (k == 0) throw 5;
        if (k == 1) throw 2.5;
        if (k == 2) throw Der();
        if (k == 3) throw "text";
        if (k == 4) throw (Base *)new Der;
        return 0;
    } catch (int i) { std::printf("int %d ", i); return 1; }
    catch (double d) { std::printf("double %g ", d); return 2; }
    catch (const Base &b) { std::printf("%s ", b.w()); return 3; }
    catch (const char *s) { std::printf("str %s ", s); return 4; }
    catch (Base *p) { std::printf("ptr %s ", p->w()); delete p; return 5; }
}
int rethrower() { try { try { throw 7; } catch (int e) { std::printf("inner "); throw e; } } catch (int v) { std::printf("outer %d ", v); return v; } return 0; }
int main() {
    for (int k = 0; k < 6; ++k) std::printf("%d\n", classify(k));
    std::printf("%d\n", rethrower());
    try { throw Der(); } catch (Der &d) { std::printf("%s\n", d.w()); }
    try { throw 1; } catch (...) { std::printf("any\n"); }
    int n = 0; for (int i = 0; i < 10; ++i) { try { if (i == 3) throw i; ++n; } catch (int) { break; } }
    std::printf("%d\n", n);
    try { throw 'c'; } catch (int) { std::printf("int?\n"); } catch (char c) { std::printf("char %c\n", c); }
    return 0;
}
