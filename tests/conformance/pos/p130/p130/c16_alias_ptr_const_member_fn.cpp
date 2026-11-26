struct S { int f() const { return 4; } };
using PF = int (S::*)() const;
int main() { PF p = &S::f; S s; return (s.*p)() == 4 ? 0 : 1; }
