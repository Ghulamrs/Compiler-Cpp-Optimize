struct A { A(int); }; struct B { B(int); }; struct D : A, B { using A::A; using B::B; }; int main() { D d(1); return 0; }
