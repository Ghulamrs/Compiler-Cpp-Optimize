struct S { ~S() = delete; };
int main() { S s; return 0; }
