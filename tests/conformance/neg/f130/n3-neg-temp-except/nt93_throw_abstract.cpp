struct A { virtual void f() = 0; }; A *p; int main() { throw *p; }
