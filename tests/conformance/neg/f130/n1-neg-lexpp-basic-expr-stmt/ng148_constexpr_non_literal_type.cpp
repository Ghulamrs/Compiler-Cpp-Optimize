struct S { S() {} int v; }; int main() { constexpr S s; return s.v; }
