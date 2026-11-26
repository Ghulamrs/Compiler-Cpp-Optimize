struct B { virtual int f() = delete; };
struct D : B { int f() { return 1; } };
int main() { D d; return d.f(); }
