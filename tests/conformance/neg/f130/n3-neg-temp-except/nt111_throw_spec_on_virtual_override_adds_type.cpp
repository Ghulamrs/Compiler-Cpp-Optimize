struct B { virtual void f() throw(int); }; struct D : B { void f() throw(int, double); }; int main() { return 0; }
