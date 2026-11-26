// Object-like and function-like macros, #, ##, nested expansion, __VA_ARGS__, defined, #if arithmetic, #elif, #undef, #line, __LINE__, __FILE__ basename.
#include <cstdio>
#include <cstring>
#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a, b) a##b
#define XCAT(a, b) CAT(a, b)
#define VALUE 42
#define SUM(...) sum(__VA_ARGS__)
#define LOG(fmt, ...) std::printf(fmt, __VA_ARGS__)
#define EMPTY
#define F(x) (x * 2)
#define G F
int sum(int a, int b, int c) { return a + b + c; }
int CAT(va, lue) = 7;
int main() {
    std::printf("%s %s %s\n", STR(VALUE), XSTR(VALUE), STR(a "b" 'c'));
    std::printf("%d %d\n", XCAT(VA, LUE), value);
    std::printf("%d\n", SUM(1, 2, 3));
    LOG("%d %s\n", 5, "five");
    std::printf("%d\n", F(3) + EMPTY 1);
#if VALUE > 40 && defined(VALUE) && !defined(NOPE)
    std::printf("if ok\n");
#elif 1
    std::printf("elif bad\n");
#else
    std::printf("else bad\n");
#endif
#if 0x10 == 16 && 010 == 8 && (1 ? 2 : 3) == 2 && -1 < 0 && 1 << 40 == 0x10000000000
    std::printf("arith ok\n");
#endif
#if -1 > 0u
    std::printf("pp unsigned ok\n");
#endif
#if 10 / 3 == 3 && 10 % 3 == 1 && ~0 == -1 && (0 || 1) && !(0 && 1)
    std::printf("ops ok\n");
#endif
#undef VALUE
#ifndef VALUE
    std::printf("undef ok\n");
#endif
#define VALUE 1
    std::printf("%d\n", VALUE);
    std::printf("%d\n", __LINE__);
#line 100 "renamed.cpp"
    std::printf("%d %s\n", __LINE__, __FILE__);
    std::printf("%s\n", STR(__LINE__));
    std::printf("%d\n", (int)std::strlen(STR(   spaced   out   )));
    return 0;
}
