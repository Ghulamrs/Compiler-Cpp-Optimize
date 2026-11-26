#include <cstdio>
struct S { int a; int f(); }; int S::f() { return 1; }
int k(int S::*) { return 1; } int k(int (S::*)()) { return 2; } int k(int (*)()) { return 3; } int k(int (*)(int)) { return 4; } int free_f() { return 0; } int free_g(int) { return 0; }
int main() { std::printf("%d %d %d %d\n", k(&S::a), k(&S::f), k(free_f), k(&free_g)); return 0; }
