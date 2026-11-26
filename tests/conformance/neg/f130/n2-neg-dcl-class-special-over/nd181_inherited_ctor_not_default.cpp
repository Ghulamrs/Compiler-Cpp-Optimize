struct A { A(int); }; struct D : A { using A::A; }; int main() { D d; return 0; }
