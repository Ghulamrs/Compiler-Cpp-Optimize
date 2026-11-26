struct S { S(); int v; };
S::S() = default;
int main() { S s; s.v = 0; return s.v; }
