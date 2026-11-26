struct S { void f(); }; int main() { S s; void (S::*p)() = &s.f; return 0; }
