struct S { int f() const { return 1; } }; template <int (S::*PF)()> int g(S &s) { return (s.*PF)(); } int main() { S s; return g<&S::f>(s); }
