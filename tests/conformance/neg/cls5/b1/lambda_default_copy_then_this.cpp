struct S { int m = 1; int f() { auto l = [=, this]() { return m; }; return l(); } };
int main() { S s; return s.f(); }
