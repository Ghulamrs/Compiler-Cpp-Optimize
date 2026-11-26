struct A { int v; }; struct B : A {}; struct C : A {}; struct D : B, C {}; int main() { D d; return d.v; }
