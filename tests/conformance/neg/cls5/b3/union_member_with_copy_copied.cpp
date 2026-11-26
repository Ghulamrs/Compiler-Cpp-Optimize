struct S { S() {} S(const S &) {} };
union U { S s; int i; U() : i(0) {} ~U() {} };
int main() { U a; U b(a); return 0; }
