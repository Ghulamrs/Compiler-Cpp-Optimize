struct S { int a[3]; S() : a{1, 2, 3, 4} {} };
int main() { S s; return s.a[0]; }
