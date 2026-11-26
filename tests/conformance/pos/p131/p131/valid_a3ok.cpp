struct A { int v; }; struct B : virtual A {}; struct C : virtual A {}; struct D : B, C {}; int main() { D d; A *a = &d; B *b = &d; return (a != 0) + (b != 0); }
