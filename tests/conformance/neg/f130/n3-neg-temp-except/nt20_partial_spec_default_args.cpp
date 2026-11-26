template <class T, class U> struct X; template <class T> struct X<T, int = int> {}; int main() { return 0; }
