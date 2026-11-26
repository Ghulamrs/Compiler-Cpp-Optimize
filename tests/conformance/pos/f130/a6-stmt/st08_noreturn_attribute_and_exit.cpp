#include <cstdio>
#include <cstdlib>
[[noreturn]] void die(int c) { std::printf("die%d\n", c); std::exit(c); }
int f(int x) { if (x) return 1; die(3); }
int main() { std::printf("%d\n", f(1)); f(0); return 0; }
