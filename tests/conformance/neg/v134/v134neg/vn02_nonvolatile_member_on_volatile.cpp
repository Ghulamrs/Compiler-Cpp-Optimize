struct S { int f() { return 1; } }; int main() { volatile S s; return s.f(); }
