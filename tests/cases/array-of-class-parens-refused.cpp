// An array of a class is initialised from a braced list; parentheses are refused, as clang refuses them.
struct S { int v; S(int a) : v(a) {} };
int main() { S a[2](1); return a[0].v; }
