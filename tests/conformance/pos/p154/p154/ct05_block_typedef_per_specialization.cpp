template <class T, class U> T g(T x, U) { typedef U P; return x + P(1); }
template <class T, class U> T f(T x, U u) { typedef T P; return P(g(x, u)); }
int main() { return (int)(f(1.0, 2.0L) + f(2.0L, 1.0)) - 5; }
