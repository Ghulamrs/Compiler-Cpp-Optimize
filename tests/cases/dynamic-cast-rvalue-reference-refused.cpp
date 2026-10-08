// `dynamic_cast` to an rvalue reference is refused by name; the lvalue
// reference form works.
struct B { virtual ~B() {} };
struct D : B { };
int main() {
    D d;
    B &b = d;
    D &&r = dynamic_cast<D &&>(b);
    (void)r;
    return 0;
}
