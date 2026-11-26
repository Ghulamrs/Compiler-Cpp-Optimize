// [except.handle]/3: a handler of a pointer type matches an exception of type std::nullptr_t, in the order written.
#include <cstddef>
#include <cstdio>
struct S { int m; };
void thrower() { throw nullptr; }
int main() {
    try {
        throw nullptr;
    } catch (int *p) {
        std::printf("int* %d\n", p == 0 ? 1 : 0);
    }
    try {
        thrower();
    } catch (const char *p) {
        std::printf("const char* %d\n", p == 0 ? 1 : 0);
    }
    try {
        thrower();
    } catch (void (*f)()) {
        std::printf("function %d\n", f == 0 ? 1 : 0);
    }
    try {
        thrower();
    } catch (S *const p) {
        std::printf("S* const %d\n", p == 0 ? 1 : 0);
    }
    try {
        thrower();
    } catch (double) {
        std::printf("double\n");
    } catch (long *p) {
        std::printf("long* %d\n", p == 0 ? 1 : 0);
    } catch (std::nullptr_t) {
        std::printf("nullptr_t\n");
    }
    try {
        thrower();
    } catch (std::nullptr_t) {
        std::printf("nullptr_t\n");
    } catch (int *) {
        std::printf("int*\n");
    }
    try {
        try {
            thrower();
        } catch (int) {
            std::printf("int\n");
        }
    } catch (void *p) {
        std::printf("void* %d\n", p == 0 ? 1 : 0);
    }
    return 0;
}
