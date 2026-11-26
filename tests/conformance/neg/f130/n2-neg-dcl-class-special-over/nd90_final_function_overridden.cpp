struct B { virtual void f() final; }; struct D : B { void f(); }; int main() { return 0; }
