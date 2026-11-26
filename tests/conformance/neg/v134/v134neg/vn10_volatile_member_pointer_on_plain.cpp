struct S { int f() { return 1; } }; int main() { volatile S s; int (S::*p)() = &S::f; return (s.*p)(); }
