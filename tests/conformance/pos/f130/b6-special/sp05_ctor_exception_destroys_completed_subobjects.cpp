#include <cstdio>
struct M { int i; M(int i) : i(i) { std::printf("+m%d ", i); } ~M() { std::printf("-m%d ", i); } }; struct B { B() { std::printf("+B "); } ~B() { std::printf("-B "); } };
struct D : B { M a, b; D() : a(1), b(2) { std::printf("+D "); throw 5; } ~D() { std::printf("-D "); } };
struct E { M a; M b; E() : a(1), b(throwing()) {} static int throwing() { throw 6; } };
int main() { try { D d; } catch (int e) { std::printf("c%d ", e); } try { E e; } catch (int e) { std::printf("c%d", e); } std::printf("\n"); return 0; }
