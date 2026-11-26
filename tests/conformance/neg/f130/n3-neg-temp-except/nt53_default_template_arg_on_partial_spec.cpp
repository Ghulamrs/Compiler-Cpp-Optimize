template <class T, class U> struct X {}; template <class T = int> struct X<T, T> {}; int main() { return 0; }
