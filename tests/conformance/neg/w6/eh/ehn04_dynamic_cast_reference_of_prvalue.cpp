// [expr.dynamic.cast]/2: for a cast to an lvalue reference the operand is an lvalue of a complete class type
struct B { virtual ~B() {} }; struct D : B {};
B make() { return B(); }
int main() { D &d = dynamic_cast<D &>(make()); (void)d; return 0; }
