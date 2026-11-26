// [dcl.array]/1, [over.load]: an array of volatile T is another type than an array of T - overloads on a reference or a pointer to each are distinct functions
#include <cstdio>
int f(int (&a)[2]) { return a[0] + 100; }
int f(volatile int (&a)[2]) { return a[0] + 200; }
int f(const volatile int (&a)[2]) { return a[0] + 300; }
int g(int (*)[2]) { return 10; }
int g(volatile int (*)[2]) { return 20; }
int g(const int (*)[2]) { return 30; }
int main() {
    int a[2] = { 1, 2 };
    volatile int b[2] = { 3, 4 };
    const volatile int c[2] = { 5, 6 };
    const int d[2] = { 7, 8 };
    std::printf("%d %d %d %d %d %d\n", f(a), f(b), f(c), g(&a), g(&b), g(&d));
    return 0;
}