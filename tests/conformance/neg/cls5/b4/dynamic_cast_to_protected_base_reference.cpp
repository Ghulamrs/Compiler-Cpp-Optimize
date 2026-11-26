struct B { virtual ~B() {} };
struct D : protected B {};
int main() { D d; B &b = dynamic_cast<B &>(d); (void)b; return 0; }
