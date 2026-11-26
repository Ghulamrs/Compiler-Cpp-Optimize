struct A; struct B { int peek(const A &a); };
struct A { private: int v = 7; friend int B::peek(const A &); };
int B::peek(const A &a) { return a.v; }
int main() { A a; B b; return b.peek(a) == 7 ? 0 : 1; }
