// A25: cl's C2385 - `x` is in both bases and nothing says which.
struct A { int x; }; struct B { int x; }; struct C : A, B {};
int main() { C c; c.x = 1; return c.x; }
