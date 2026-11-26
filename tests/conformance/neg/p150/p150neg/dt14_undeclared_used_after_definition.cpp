// [temp.res]/10: later is declared after the template, and the call does not depend on T.
template <class T> int f(T) { return later(); }
int later() { return 1; }
int main() { return f(0); }
