struct B { virtual void f() final {} };
struct D : B { void f() override {} };
int main() { return 0; }
