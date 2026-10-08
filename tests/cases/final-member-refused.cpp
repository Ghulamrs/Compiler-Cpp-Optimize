// [class.virtual]/4: a virtual function marked `final` may not be overridden
// in a derived class. clang: "declaration of 'f' overrides a 'final' function".
struct B { virtual int f() { return 0; } virtual ~B() {} };
struct D : B { int f() final { return 1; } };
struct E : D { int f() { return 2; } };
int main() { E e; return e.f(); }
