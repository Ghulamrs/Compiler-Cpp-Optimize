struct P { int x, y; };
struct S { int a[3] = {1, 2, 3}; P p{4, 5}; int b[2]{6, 7}; };
int main() { S s; return s.a[2] + s.p.y + s.b[1] == 15 ? 0 : 1; }
