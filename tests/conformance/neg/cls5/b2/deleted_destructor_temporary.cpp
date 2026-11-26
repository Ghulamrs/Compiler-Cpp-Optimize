struct S { int v; ~S() = delete; };
int main() { return S().v; }
