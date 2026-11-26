struct S { const int c; S(int c) : c(c) {} }; int main() { S a(1), b(2); a = b; return a.c; }
