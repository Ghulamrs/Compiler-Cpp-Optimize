#include <cstdio>
class S { void f(int) {} public: void f(double) { std::printf("double "); } void f(const char *) { std::printf("cstr\n"); } };
int main() { S s; s.f(1.5); s.f("x"); return 0; }
