struct S { int m; int f() { return 1; } }; template <int S::*PM> int g(S &s) { return s.*PM; } int main() { S s; return g<&S::f>(s); }
