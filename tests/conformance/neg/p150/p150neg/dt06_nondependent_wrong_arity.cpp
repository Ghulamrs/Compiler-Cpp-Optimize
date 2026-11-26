// [temp.res]/8: g takes one int, and g(1, 2) depends on nothing.
int g(int);
template <class T> int f(T) { return g(1, 2); }
int main() { return 0; }
