#include <cstdio>
struct T { int id; T(int i) : id(i) { std::printf("+%d ", id); } ~T() { std::printf("-%d ", id); } const T &self() const { return *this; } int get() const { return id; } };
const T &pass(const T &t) { return t; }
int main() { const T &a = T(1); std::printf("a "); { T &&b = T(2); std::printf("b%d ", b.id); } { const T &c = pass(T(3)); std::printf("c "); (void)c; } { int v = T(4).self().get(); std::printf("v%d ", v); } { const int &r = T(5).id; std::printf("r%d ", r); } std::printf("end\n"); return 0; }
