struct A { A(int); }; struct D { using A::A; }; int main() { return 0; }
