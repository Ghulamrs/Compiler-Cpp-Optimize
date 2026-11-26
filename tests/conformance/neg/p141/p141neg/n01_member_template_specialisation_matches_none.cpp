// [temp.expl.spec]: S<int> has no member template h taking a char and returning a double.
template <class T> struct S { template <class U> int g(U) { return 1; } };
template <> template <> double S<int>::g(char) { return 2; }
int main() { return S<int>().g('x'); }