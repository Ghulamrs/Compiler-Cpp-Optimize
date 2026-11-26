struct S { int v; S() : v(1) {} };
union U { S s; int i; U() : s() {} ~U() {} };
int main() { U u; return u.s.v == 1 ? 0 : 1; }
