struct S { void f(); }; int main() { S s; void (*p)() = s.f; return 0; }
