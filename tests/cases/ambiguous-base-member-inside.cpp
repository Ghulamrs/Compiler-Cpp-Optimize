// A25, from inside a member function: the unqualified name is still ambiguous.
struct A { int x; }; struct B { int x; }; struct C : A, B { int f() { return x; } };
int main() { C c; return c.f(); }
