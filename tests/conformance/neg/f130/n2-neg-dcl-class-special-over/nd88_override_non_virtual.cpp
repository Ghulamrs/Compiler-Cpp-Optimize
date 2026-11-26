struct B { void f(); }; struct D : B { void f() override; }; int main() { return 0; }
