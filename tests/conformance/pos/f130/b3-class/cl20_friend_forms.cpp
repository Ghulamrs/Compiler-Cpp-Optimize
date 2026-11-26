#include <cstdio>
class A; class B { int secret = 2; friend class A; friend int peek(const B &); friend struct Nested; template <class T> friend int tpeek(const B &); }; int peek(const B &b) { return b.secret; } class A { public: int look(const B &b) { return b.secret + 10; } }; template <class T> int tpeek(const B &b) { return b.secret + (int)sizeof(T); }
class C { int v = 5; friend class D; }; class D { struct Inner { int get(const C &c) { return c.v; } }; public: int go(const C &c) { return Inner().get(c); } };
int main() { B b; A a; C c; D d; std::printf("%d %d %d %d\n", peek(b), a.look(b), tpeek<double>(b), d.go(c)); return 0; }
