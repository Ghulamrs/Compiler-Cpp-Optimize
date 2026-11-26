struct M { M() {} M(const M &) = delete; }; struct S { M m; }; int main() { S a; S b(a); return 0; }
