// [temp.expl.spec]/6: a member class specialized after a use that instantiated it.
template <class T> struct S { struct In { int v; }; };
S<int>::In early;
template <> struct S<int>::In { long w; };
int main() { return 0; }
