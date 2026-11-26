#include <cstdio>
struct S { int v; S(int v) : v(v) { std::printf("+%d ", v); } S() : S(1) { std::printf("body "); throw 2; } ~S() { std::printf("~%d ", v); } };
int main() { try { S s; } catch (int e) { std::printf("c%d\n", e); } return 0; }
