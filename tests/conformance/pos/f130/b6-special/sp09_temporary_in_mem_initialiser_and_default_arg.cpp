#include <cstdio>
struct T { int id; T(int i) : id(i) { std::printf("+%d ", id); } ~T() { std::printf("-%d ", id); } int get() const { return id; } };
struct H { int v; H(int x = T(1).get()) : v(x + T(2).get()) { std::printf("body "); } };
int main() { H h; std::printf("%d\n", h.v); return 0; }
