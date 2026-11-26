// [temp.variadic]/5: a mem-initializer naming a pack of bases without the '...' that expands it.
struct A { A(int) {} };
template <class... Bs> struct M : Bs... { M() : Bs(1) {} };
int main() { M<A> m; (void)m; return 0; }
