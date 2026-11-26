struct P { int v; };
int main() { constexpr const P &r = P{1}; return r.v; }
