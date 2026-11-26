// [temp.expl.spec]: 'Missing' is no member class of S<int>.
template <class T> struct S { struct In { int v; }; };
template <> struct S<int>::Missing { int w; };
int main() { return 0; }
