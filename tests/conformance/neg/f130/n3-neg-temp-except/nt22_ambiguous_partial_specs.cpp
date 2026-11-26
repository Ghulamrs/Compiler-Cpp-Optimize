template <class T, class U> struct X {}; template <class T> struct X<T, int> {}; template <class U> struct X<int, U> {}; int main() { X<int, int> x; return 0; }
