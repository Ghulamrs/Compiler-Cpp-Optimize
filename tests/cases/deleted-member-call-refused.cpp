// The member door to the same rule as deleted-function-refused: `g(int)` is
// deleted, `g(double)` is not, and `s.g(1)` picks the deleted one. clang: "call
// to deleted member function 'g'" (5:21).
struct S { void g(int) = delete; void g(double) {} };
int main() { S s; s.g(1); return 0; }
