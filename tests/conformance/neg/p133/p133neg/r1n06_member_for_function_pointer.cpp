struct S { int f() { return 1; } }; template <int (*F)()> int g() { return F(); } int main() { return g<&S::f>(); }
