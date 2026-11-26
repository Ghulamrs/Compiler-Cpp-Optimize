struct S { template <class T> T id(T t); };
template <class T> T S::id(T t) { return t; }
int main() { S s; return s.id(3) == 3 ? 0 : 1; }
