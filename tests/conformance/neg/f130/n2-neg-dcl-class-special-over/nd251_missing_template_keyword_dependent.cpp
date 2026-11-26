template <class T> struct A { template <class U> struct B {}; }; template <class T> void f() { typename A<T>::B<int> b; } int main() { f<int>(); return 0; }
