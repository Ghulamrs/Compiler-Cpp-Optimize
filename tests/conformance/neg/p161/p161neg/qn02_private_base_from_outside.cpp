// [class.access.base]: outside the class its private base is not to be named through an object.
struct B { int f() { return 1; } };
struct P : private B {};
int main() { P p; return p.B::f(); }
