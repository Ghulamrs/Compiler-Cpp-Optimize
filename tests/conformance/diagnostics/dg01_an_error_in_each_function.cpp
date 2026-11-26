// three errors, in three functions
int f() { return undefined_a; }
int g() { int x = undefined_b; return x + 1; }
struct S { int m; void h() { m = undefined_c; } };
int main() { return f() + g(); }
