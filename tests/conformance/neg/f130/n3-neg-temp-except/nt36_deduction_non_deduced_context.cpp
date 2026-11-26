template <class T> void f(typename T::type); struct S { typedef int type; }; int main() { f(1); return 0; }
