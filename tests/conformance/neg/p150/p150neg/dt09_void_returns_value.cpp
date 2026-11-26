// [stmt.return]/2: a void function returns no value, and the 1 depends on nothing.
template <class T> void f(T) { return 1; }
int main() { return 0; }
