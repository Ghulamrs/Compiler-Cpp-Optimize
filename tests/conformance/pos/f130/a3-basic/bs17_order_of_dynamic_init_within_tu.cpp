#include <cstdio>
int f(const char *s) { std::printf("%s ", s); return 1; }
int a = f("a"); namespace { int b = f("b"); } struct S { static int c; }; int S::c = f("c"); int d = f("d");
int main() { std::printf("\n"); return a + b + S::c + d - 4; }
