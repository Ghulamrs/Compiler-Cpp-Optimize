// [temp.res]/10: a non-dependent name must be declared before the template that uses it.
template <class T> int f(T) { return later(1); }
int later(int);
int main() { return f(1); }
int later(int v) { return v; }
