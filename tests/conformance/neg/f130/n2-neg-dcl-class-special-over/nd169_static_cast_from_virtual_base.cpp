struct B {}; struct D : virtual B {}; int main() { D d; B *b = &d; D *p = static_cast<D*>(b); return 0; }
