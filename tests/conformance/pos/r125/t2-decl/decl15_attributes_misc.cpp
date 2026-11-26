// [dcl.attr]: [[noreturn]], [[carries_dependency]], unknown attributes ignored (clang warns, cl ignores with /W3? use unknown only on cpp11-tolerant placement), alignas on variables and members, alignof.
#include <cstdio>
#include <cstdlib>
[[noreturn]] void die() { std::exit(0); }
struct alignas(16) A16 { char c; };
struct B { char c; alignas(8) char d; };
struct alignas(32) C32 { int x; };
int main() {
    alignas(16) char buf[20];
    alignas(double) char dbuf[1];
    std::printf("%d %d %d %d\n", (int)sizeof(A16), (int)alignof(A16), (int)sizeof(B), (int)alignof(B));
    std::printf("%d %d %d\n", (int)sizeof(C32), (int)alignof(C32), (int)alignof(int));
    std::printf("%d %d\n", (int)(((unsigned long long)buf) % 16), (int)(((unsigned long long)dbuf) % 8));
    std::printf("%d %d %d %d\n", (int)alignof(char), (int)alignof(long long), (int)alignof(double), (int)alignof(long double));
    std::printf("before die\n");
    die();
}
