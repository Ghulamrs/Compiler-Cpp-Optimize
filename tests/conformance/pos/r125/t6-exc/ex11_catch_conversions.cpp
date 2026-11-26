// [except.handle]/3: a handler matches by qualification conversion, derived-to-base, pointer to void*, but NOT by integral conversion or user conversion; nullptr thrown as nullptr_t; array thrown decays.
#include <cstdio>
#include <cstddef>
struct A { virtual ~A() {} }; struct B : A {};
struct Conv { operator int() const { return 1; } };
int main() {
    try { throw 'c'; } catch (int) { std::printf("int-wrong "); } catch (char) { std::printf("char "); }
    try { throw 1; } catch (long) { std::printf("long-wrong "); } catch (unsigned) { std::printf("unsigned-wrong "); } catch (int) { std::printf("int "); }
    try { throw Conv(); } catch (int) { std::printf("int-wrong "); } catch (Conv &) { std::printf("Conv "); }
    try { int *p = 0; throw p; } catch (void *) { std::printf("void* "); }
    try { const int *p = 0; throw p; } catch (void *) { std::printf("void*-wrong "); } catch (const void *) { std::printf("const-void* "); }
    try { B *b = new B; throw b; } catch (A *a) { std::printf("A* "); delete a; }
    try { throw nullptr; } catch (int *) { std::printf("int* "); } catch (std::nullptr_t) { std::printf("nullptr_t-wrong "); }
    try { throw "arr"; } catch (const char *s) { std::printf("%s ", s); }
    try { throw 1.0f; } catch (double) { std::printf("double-wrong "); } catch (float) { std::printf("float "); }
    try { throw B(); } catch (A) { std::printf("A-by-value "); }
    std::printf("\n");
    return 0;
}
