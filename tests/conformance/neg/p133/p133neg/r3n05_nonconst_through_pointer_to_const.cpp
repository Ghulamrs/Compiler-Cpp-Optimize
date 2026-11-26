struct S { int f() { return 1; } }; int main() { S s; const S *cp = &s; int (S::*p)() = &S::f; return (cp->*p)(); }
