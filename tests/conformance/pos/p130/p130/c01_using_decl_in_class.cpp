struct B { int f(int) { return 1; } };
struct D : B { using B::f; int f(double) { return 2; } };
int main() { D d; return d.f(1) == 1 && d.f(1.0) == 2 ? 0 : 1; }
