struct B { int v; }; struct D : virtual B {}; int main() { int B::*p = &B::v; int D::*q = p; return 0; }
