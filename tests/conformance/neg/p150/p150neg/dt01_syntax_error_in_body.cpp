// [temp.res]/8: a template body that is not a well-formed sequence of tokens is ill-formed where it is written.
template <class T> int f(T) { int x = ; return x; }
int main() { return 0; }
