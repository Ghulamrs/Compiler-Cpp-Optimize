struct S { int *p; void f() const { p = nullptr; } }; int main() { return 0; }
