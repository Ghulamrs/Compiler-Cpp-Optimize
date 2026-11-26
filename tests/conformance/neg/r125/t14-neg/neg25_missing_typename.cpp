struct S { typedef int type; };
template <class T> int f() { T::type x = 1; return x; }
int main() { return f<S>(); }
