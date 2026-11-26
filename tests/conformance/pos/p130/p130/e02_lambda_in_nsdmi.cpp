struct S { int (*f)(int) = [](int x) { return x + 1; }; };
int main() { S s; return s.f(1) == 2 ? 0 : 1; }
