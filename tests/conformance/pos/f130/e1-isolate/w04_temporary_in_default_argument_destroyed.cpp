#include <cstdio>
struct T { int id; T(int i) : id(i) { std::printf("+%d ", id); } ~T() { std::printf("-%d ", id); } int get() const { return id; } };
int f(int x = T(1).get()) { std::printf("f "); return x; }
int main() { int r = f(); std::printf("%d\n", r); return 0; }
