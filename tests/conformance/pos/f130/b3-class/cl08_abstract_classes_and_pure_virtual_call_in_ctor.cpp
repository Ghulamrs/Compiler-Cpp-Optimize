#include <cstdio>
struct A { A() { std::printf("A:%d ", nonpure()); } virtual int nonpure() { return 0; } virtual int pure() = 0; virtual ~A() {} };
struct C : A { int nonpure() override { return 1; } int pure() override { return 2; } };
int main() { C c; A &a = c; std::printf("%d %d\n", a.nonpure(), a.pure()); return 0; }
