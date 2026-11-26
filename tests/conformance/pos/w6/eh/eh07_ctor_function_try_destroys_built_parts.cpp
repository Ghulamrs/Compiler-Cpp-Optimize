// [except.handle]/11,15: the bases and members already built are destroyed before a constructor's function-try handler, which then rethrows
#include <cstdio>
struct B { B() { std::printf("B "); } ~B() { std::printf("~B "); } };
struct M { int i; M(int i) : i(i) { if (i == 2) throw i; std::printf("M%d ", i); } ~M() { std::printf("~M%d ", i); } };
struct S : B { M a, b; S() try : a(1), b(2) { std::printf("body "); } catch (int e) { std::printf("handler%d ", e); } };
int main() { try { S s; } catch (int e) { std::printf("outer%d\n", e); } return 0; }
