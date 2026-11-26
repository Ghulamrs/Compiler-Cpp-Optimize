// [stmt.break]/1: break outside a loop or switch, in a template never instantiated.
template <class T> void f(T) { break; }
int main() { return 0; }
