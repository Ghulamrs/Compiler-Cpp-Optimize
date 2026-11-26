struct S { void f() &&; }; int main() { S s; s.f(); return 0; }
