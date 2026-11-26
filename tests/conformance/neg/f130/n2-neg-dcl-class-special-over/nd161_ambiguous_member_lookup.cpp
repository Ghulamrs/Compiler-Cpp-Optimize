struct A { int v; }; struct B { int v; }; struct C : A, B {}; int main() { C c; return c.v; }
