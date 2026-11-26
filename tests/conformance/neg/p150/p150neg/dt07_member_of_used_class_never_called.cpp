// [temp.res]/10: S<int> is made, m never is - its body names a function declared nowhere.
template <class T> struct S { int v; int m() { return undeclared(v); } };
int main() { S<int> s; s.v = 0; return s.v; }
