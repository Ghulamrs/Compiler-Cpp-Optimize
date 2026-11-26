struct S { S() {} S(const S &) = delete; }; int main() { S a; S b = a; return 0; }
