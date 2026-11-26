// [temp.res]/10: Undeclared names no type, and depends on nothing.
template <class T> void f(T) { Undeclared u; (void)u; }
int main() { return 0; }
