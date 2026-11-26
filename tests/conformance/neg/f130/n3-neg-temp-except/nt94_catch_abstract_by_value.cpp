struct A { virtual void f() = 0; }; int main() { try {} catch (A a) {} return 0; }
