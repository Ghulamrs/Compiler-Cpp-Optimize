struct S { int f() { return 1; } }; int main() { const S cs = S(); int (S::*p)() = &S::f; return (cs.*p)(); }
