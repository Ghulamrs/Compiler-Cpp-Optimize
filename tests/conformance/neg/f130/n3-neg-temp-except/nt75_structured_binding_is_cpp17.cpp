struct S { int a, b; }; int main() { S s{1, 2}; auto [a, b] = s; return a + b; }
