class B { protected: int f() { return 1; } };
class D : public B { protected: using B::f; };
int main() { D d; return d.f(); }
