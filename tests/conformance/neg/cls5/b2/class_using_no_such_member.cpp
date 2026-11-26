struct B { int f() { return 1; } };
struct D : B { using B::g; };
int main() { D d; return 0; }
