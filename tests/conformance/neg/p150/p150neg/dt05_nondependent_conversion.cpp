// [temp.res]/8: a double initialising an int * depends on nothing - no specialization can be valid.
template <class T> void f(T) { int *p = 1.5; (void)p; }
int main() { return 0; }
