struct S { S() {} ~S() {} }; int main() { int n = 1; switch (n) { case 0: S s; break; case 1: return 1; } return 0; }
