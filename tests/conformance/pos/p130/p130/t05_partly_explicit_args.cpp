struct Obj { int v; Obj(int a, int b) : v(a + b) {} };
template <class T, class... A> T make(A... a) { return T(a...); }
template <class T, class U> T conv(U u) { return T(u); }
int main() { Obj o = make<Obj>(1, 2); return o.v == 3 && conv<int>(2.5) == 2 ? 0 : 1; }
