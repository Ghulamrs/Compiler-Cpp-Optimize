struct A;
struct B { int peek(const A &a); int other(const A &a); };
class A { int v = 1; friend int B::peek(const A &); };
int B::peek(const A &a) { return a.v; }
int B::other(const A &a) { return a.v; }
int main() { return 0; }
