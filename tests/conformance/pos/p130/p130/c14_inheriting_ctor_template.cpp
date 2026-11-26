struct B { int v; template <class T> B(T t) : v(int(t)) {} };
struct D : B { using B::B; };
int main() { D d(2.5); return d.v == 2 ? 0 : 1; }
