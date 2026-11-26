// [temp.res]/3: T::type names a type only with typename in front of it.
template <class T> void f() { T::type *x = 0; (void)x; }
int main() { return 0; }
