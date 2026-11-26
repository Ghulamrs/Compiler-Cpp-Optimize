struct A {}; struct B : A {}; struct C : B { C() : A() {} }; int main() { return 0; }
