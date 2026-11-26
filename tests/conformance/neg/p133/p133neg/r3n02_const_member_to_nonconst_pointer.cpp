struct S { int f() const { return 1; } }; int main() { int (S::*p)() = &S::f; S s; return (s.*p)(); }
