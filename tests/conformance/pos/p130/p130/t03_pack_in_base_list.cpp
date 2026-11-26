struct A { int a; }; struct B { int b; };
template <class... Bs> struct M : Bs... {};
int main() { M<A, B> m; m.a = 1; m.b = 2; return m.a + m.b == 3 ? 0 : 1; }
