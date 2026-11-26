// [class.abstract]/3: a class that leaves the second base's pure virtual unoverridden is abstract
struct A { virtual int f() { return 1; } };
struct B { virtual int g() = 0; };
struct D : A, B { int f() { return 2; } };
int main() { D d; return d.f(); }
