// [temp.variadic]/5: a pack named in a base-specifier-list without the '...' that expands it.
struct A {};
template <class... Bs> struct M : Bs {};
int main() { M<A> m; (void)m; return 0; }
