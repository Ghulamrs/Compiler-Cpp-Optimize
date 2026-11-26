// [expr.dynamic.cast]/6: a downcast to a reference needs a polymorphic operand class
struct B { int b; }; struct D : B {};
int main() { D d; B &b = d; D &r = dynamic_cast<D &>(b); (void)r; return 0; }
