// More initialisers than elements: clang says "excess elements in array initializer".
struct S { int v; S(int a) : v(a) {} };
int main() { S a[2] = { 1, 2, 3 }; return a[0].v; }
