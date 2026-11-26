struct V { virtual int f() { return 1; } virtual ~V() {} };
struct A : virtual V {}; struct B : virtual V {};
struct C : A, B { int f() { return 5; } };
int main() { C c; V *v = &c; return v->f() == 5 ? 0 : 1; }
