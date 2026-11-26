#include <cstdio>
int main() { int x = 1; { int x = 2; { int x = 3; std::printf("%d", x); } std::printf("%d", x); } std::printf("%d", x); for (int i = 0; i < 2; i++) { int i2 = i * 2; std::printf("%d", i2); } int i = 7; std::printf("%d", i); for (;;) break; while (false) ; do { } while (0); std::printf("\n"); return 0; }
