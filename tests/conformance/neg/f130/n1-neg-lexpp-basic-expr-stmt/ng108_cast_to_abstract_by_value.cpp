struct A { virtual void f() = 0; }; int main() { A *p = nullptr; (void)static_cast<A>(*p); return 0; }
