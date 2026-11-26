struct S { S(); };
S::S() = default;
S::S() = default;
int main() { S s; return 0; }
