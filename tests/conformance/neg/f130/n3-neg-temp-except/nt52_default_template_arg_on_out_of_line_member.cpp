template <class T = int> struct X { void f(); }; template <class T = int> void X<T>::f() {} int main() { return 0; }
