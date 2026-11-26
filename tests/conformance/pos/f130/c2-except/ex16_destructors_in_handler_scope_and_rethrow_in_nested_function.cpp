#include <cstdio>
struct T { int i; T(int i) : i(i) {} ~T() { std::printf("~%d ", i); } }; void rethrow() { throw; }
int main() { try { try { T a(1); throw 5; } catch (int) { T b(2); rethrow(); } } catch (int e) { std::printf("%d\n", e); } return 0; }
