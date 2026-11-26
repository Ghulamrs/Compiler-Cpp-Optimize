struct S { int b : 3; }; int main() { S s; int *p = &s.b; return *p; }
