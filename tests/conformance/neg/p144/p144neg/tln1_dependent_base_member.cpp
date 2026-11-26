// [temp.dep]/3: m is a member of the dependent base B<T> alone, so unqualified m names nothing.
template <class T> struct B { int m; };
template <class T> struct D : B<T> { int f() { return m; } };
int main() { D<int> d; return d.f(); }
