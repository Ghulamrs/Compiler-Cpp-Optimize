struct S { ~S() = delete; };
int main() { S *p = new S; (void)p; return 0; }
