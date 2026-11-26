struct S { S() {} S(S &&) {} }; int main() { S a; S b = a; return 0; }
