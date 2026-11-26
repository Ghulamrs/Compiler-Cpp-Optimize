struct R { const int *p; int n; constexpr R(const int *q, int m) : p(q), n(m) {} };
int g;
constexpr R r(&g, 2);
static_assert(r.n == 2, "n");
int main() { return r.p == &g ? 0 : 1; }
