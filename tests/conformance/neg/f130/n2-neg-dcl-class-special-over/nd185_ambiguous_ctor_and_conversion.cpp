struct B; struct A { A(const B &); }; struct B { operator A() const; }; void f(A); int main() { B b; f(b); return 0; }
