template <class T, class U> int f(T, U) { return 0; }
template <class T> int f<T, int>(T, int) { return 1; }
int main() { return f(1, 2); }
