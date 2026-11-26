template <class T, class U> struct P2 { T a; U b; };
template <class T> using Same = P2<T, T>;
int main() { Same<int> s; s.a = 1; s.b = 2; return s.a + s.b == 3 ? 0 : 1; }
