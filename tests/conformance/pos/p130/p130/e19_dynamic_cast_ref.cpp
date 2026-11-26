struct B { virtual ~B() {} }; struct D : B {};
int main() { D d; B &b = d; D &r = dynamic_cast<D &>(b); return &r == &d ? 0 : 1; }
