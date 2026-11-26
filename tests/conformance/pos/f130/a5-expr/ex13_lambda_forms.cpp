#include <cstdio>
int main() { int a = 1, b = 2; auto l1 = [a, &b]() mutable -> int { a++; b++; return a + b; }; auto l2 = [](int x) { return x * 2; }; int (*fp)(int) = l2; auto l3 = [] { return 3; }; auto l4 = [&] { return a + b; }; auto l5 = [=]() { return a; };
int r1 = l1(); int r2 = l1(); std::printf("%d %d %d %d %d %d %d %d\n", r1, r2, a, b, fp(4), l3(), l4(), l5()); return 0; }
