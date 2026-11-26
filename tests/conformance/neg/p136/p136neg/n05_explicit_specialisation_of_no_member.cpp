// [temp.expl.spec]/1: S<int> has no member h to specialize.
template <class T> struct S { int f() { return 1; } };
template <> int S<int>::h() { return 2; }
int main() { return S<int>().f(); }