struct B { int b; }; struct D : B { int d; };
int main() { D x; x.d = 3; B &rb = x; D &rd = static_cast<D &>(rb); return rd.d == 3 ? 0 : 1; }
