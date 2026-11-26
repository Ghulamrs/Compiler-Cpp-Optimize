struct S { S() {} };
union U { S s; int i; };
int main() { U u; return 0; }
