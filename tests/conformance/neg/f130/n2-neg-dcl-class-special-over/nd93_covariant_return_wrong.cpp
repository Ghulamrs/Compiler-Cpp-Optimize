struct A {}; struct B {}; struct X { virtual A *f(); }; struct Y : X { B *f(); }; int main() { return 0; }
