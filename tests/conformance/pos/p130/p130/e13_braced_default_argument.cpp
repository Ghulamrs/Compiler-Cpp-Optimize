struct P { int a, b; };
int f(P p = {1, 2}) { return p.a + p.b; }
int main() { return f() == 3 ? 0 : 1; }
