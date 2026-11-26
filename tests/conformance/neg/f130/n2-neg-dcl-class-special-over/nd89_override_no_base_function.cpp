struct B { virtual void f(int); }; struct D : B { void f() override; }; int main() { return 0; }
