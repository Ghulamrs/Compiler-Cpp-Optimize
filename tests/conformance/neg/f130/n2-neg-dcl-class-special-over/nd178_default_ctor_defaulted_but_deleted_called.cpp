struct S { int &r; S() = default; }; int main() { S s; return 0; }
