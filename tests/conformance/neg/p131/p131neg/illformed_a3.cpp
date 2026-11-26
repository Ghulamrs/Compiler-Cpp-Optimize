struct A { int v; }; struct B : A {}; struct C : A {}; struct D : B, C {}; int main() { D d; A *a = &d; return a != 0; }
