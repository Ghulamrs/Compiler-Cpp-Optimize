// [temp.dep]/3: get is a member function of the dependent base alone.
template <class T> struct B { int get() { return 1; } };
template <class T> struct D : B<T> { int f() { return get(); } };
int main() { D<int> d; return d.f(); }
