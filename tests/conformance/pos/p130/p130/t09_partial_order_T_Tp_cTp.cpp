template <class T> int f(T) { return 1; }
template <class T> int f(T *) { return 2; }
template <class T> int f(const T *) { return 3; }
int main() { int i = 0; const int c = 0; return f(i) == 1 && f(&i) == 2 && f(&c) == 3 ? 0 : 1; }
