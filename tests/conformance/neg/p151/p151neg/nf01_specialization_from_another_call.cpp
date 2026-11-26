// [temp.over]/1: f<int>, made by f(1, 1), is no candidate of f(1, 2.0), whose T is deduced int and double.
template <class T> int f(T, T) { return 1; }
int main() { int a = f(1, 1); int b = f(1, 2.0); return a + b; }
