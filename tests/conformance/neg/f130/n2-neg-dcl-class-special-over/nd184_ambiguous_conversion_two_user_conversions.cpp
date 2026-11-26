struct A { operator int() const; operator long() const; }; void f(double); int main() { A a; f(a); return 0; }
