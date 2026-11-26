// A virtual call from a destructor resolves to the class being destroyed, not the most derived.
#include <cstdio>
struct B { virtual const char *who() const { return "B"; } ~B() { std::printf("%s ", who()); } };
struct D : B { const char *who() const override { return "D"; } ~D() { std::printf("%s ", who()); } };
int main() { { D d; } std::printf("\n"); return 0; }
