struct V { int v; V() : v(1) {} };
struct S : virtual V { int w; S(int x) : w(x) {} S() : S(5) {} };
int main() { S s; return s.w + s.v == 6 ? 0 : 1; }
