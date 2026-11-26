struct A {}; struct B {}; int main() { bool c = true; A a; B b; (c ? a : b); return 0; }
