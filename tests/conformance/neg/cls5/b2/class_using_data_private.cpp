struct B { int x = 1; };
class D : public B { private: using B::x; };
int main() { D d; return d.x; }
