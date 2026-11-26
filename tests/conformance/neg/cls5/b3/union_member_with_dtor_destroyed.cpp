struct S { ~S() {} };
union U { S s; int i; U() : i(0) {} };
int main() { U u; return u.i; }
