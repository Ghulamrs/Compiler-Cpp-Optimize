int g(double) { return 1; }
template <class T> int call(T) { return g(1); }
int g(int) { return 2; }
int main() { return call(0) == 1 ? 0 : 1; }
