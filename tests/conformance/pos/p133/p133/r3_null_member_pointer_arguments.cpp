#include <cstdio>
struct S { int g() { return 2; } int m; };
int t3(int (S::*p)()) { return p == 0 ? 1 : 2; }
int t4(int S::*p) { return p == 0 ? 3 : 4; }
int t5(int (S::*p)() const) { return p == nullptr ? 5 : 6; }
int main() { std::printf("%d %d %d %d %d %d\n", t3(0), t3(nullptr), t3(&S::g), t4(0), t4(&S::m), t5(0)); return 0; }
