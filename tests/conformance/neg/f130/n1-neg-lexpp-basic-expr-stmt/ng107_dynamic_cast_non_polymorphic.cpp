struct B {}; struct D : B {}; int main() { B b; D *d = dynamic_cast<D*>(&b); return d != 0; }
