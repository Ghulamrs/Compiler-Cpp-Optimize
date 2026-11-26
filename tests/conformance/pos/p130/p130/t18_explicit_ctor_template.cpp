struct S { int v; template <class T> explicit S(T t) : v(int(t)) {} };
int main() { S s(2.5); return s.v == 2 ? 0 : 1; }
