// [lex.nullptr], [conv.ptr]: nullptr converts to any pointer and pointer-to-member, to bool only in direct-initialisation; sizeof(nullptr_t) == sizeof(void*); overload picks pointer.
#include <cstdio>
#include <cstddef>
void f(int) { std::printf("f(int)\n"); }
void f(int *) { std::printf("f(int*)\n"); }
void f(std::nullptr_t) { std::printf("f(nullptr_t)\n"); }
void g(int) { std::printf("g(int)\n"); }
void g(char *) { std::printf("g(char*)\n"); }
struct S { int m; void mf() {} };
int main() {
    int *p = nullptr; int S::*mp = nullptr; void (S::*mf)() = nullptr;
    std::printf("%d %d %d %d\n", p == 0, mp == nullptr, mf == nullptr, p == nullptr);
    std::printf("%d\n", (int)sizeof(std::nullptr_t));
    f(0); f(nullptr); f(p); g(0); g(nullptr);
    bool b(nullptr);
    std::printf("%d\n", b);
    if (p) std::printf("bad\n"); else std::printf("null ok\n");
    std::nullptr_t n = nullptr; const char *s = n;
    std::printf("%d %d\n", s == 0, n == nullptr);
    std::printf("%d\n", !nullptr);
    return 0;
}
