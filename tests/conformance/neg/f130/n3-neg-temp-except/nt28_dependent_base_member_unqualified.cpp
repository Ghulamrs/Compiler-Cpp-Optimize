template <class T> struct B { int m; }; template <class T> struct D : B<T> { int f() { return m; } }; int main() { D<int> d; return d.f(); }
