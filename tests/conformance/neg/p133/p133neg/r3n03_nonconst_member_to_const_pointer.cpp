struct S { int f() { return 1; } }; int main() { int (S::*p)() const = &S::f; S s; return (s.*p)(); }
