struct B { int f() { return 1; } };
class D : public B { using B::f; };
int main() { D d; return d.f(); }
