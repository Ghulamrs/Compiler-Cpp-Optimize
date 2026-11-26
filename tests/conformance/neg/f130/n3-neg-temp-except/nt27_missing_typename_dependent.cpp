template <class T> void f() { T::type *p; p = 0; } struct S { typedef int type; }; int main() { f<S>(); return 0; }
