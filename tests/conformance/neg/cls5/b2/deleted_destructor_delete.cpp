struct S { ~S() = delete; };
int main() { S *p = new S; delete p; return 0; }
