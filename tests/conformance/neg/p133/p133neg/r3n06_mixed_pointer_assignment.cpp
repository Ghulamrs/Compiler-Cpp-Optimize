struct S { int f() { return 1; } int g() const { return 2; } }; int main() { int (S::*p)() const = &S::g; int (S::*q)() = &S::f; q = p; S s; return (s.*q)(); }
