struct S { int f(); };
int S::f() = default;
int main() { return 0; }
