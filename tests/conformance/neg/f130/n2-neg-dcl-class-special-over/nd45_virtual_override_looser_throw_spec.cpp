struct B { virtual void f() throw(); }; struct D : B { void f() throw(int); }; int main() { return 0; }
