struct S { int g(int); }; template <class T> int f(T t) { return t.g(); } int main() { return f(S()); }
