#include <cstdio>
#include <cstddef>
int f(int) { return 1; } int f(int *) { return 2; } int f(std::nullptr_t) { return 3; }
int main() { std::nullptr_t n = nullptr; int *p = n; bool b(nullptr); std::printf("%d %d %d %d %d\n", f(nullptr), f(0), f(n), (int)(p == nullptr), (int)b); std::printf("%d\n", (int)sizeof(nullptr)); return 0; }
