// [stmt.switch]/2: the condition may be a class with a single non-explicit conversion to an integral type.
#include <cstdio>
struct N { int v; operator int() const { return v; } };
int main() { N n = {2}; switch (n) { case 1: std::printf("one\n"); break; case 2: std::printf("two\n"); break; default: std::printf("other\n"); } return 0; }
