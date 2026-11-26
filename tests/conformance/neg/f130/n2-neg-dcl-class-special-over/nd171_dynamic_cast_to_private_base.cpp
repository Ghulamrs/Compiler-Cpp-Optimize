struct B { virtual ~B() {} }; struct D : private B {}; int main() { D d; B *b = dynamic_cast<B*>(&d); return 0; }
