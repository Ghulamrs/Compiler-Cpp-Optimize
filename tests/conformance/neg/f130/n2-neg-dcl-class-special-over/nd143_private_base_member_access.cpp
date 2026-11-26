struct B { int v; }; struct D : private B {}; int main() { D d; return d.v; }
