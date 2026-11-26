struct A { A(int); }; struct B { B(A); }; void f(B); int main() { f(1); return 0; }
