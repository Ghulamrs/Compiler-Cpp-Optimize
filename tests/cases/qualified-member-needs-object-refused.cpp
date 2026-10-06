// [expr.prim.general]/13 lets only an unevaluated operand name a non-static data member
// with no object; anywhere else it needs one.
struct S { int x; };
int main() { return S::x; }
