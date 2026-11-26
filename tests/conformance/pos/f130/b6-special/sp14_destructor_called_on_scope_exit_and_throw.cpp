#include <cstdio>
struct T { int id; T(int i) : id(i) {} ~T() { std::printf("~%d ", id); } };
void f() { T a(1); T b(2); throw 3; }
int main() { try { f(); } catch (int e) { std::printf("c%d ", e); } { T c(4); T d(5); } std::printf("\n"); return 0; }
