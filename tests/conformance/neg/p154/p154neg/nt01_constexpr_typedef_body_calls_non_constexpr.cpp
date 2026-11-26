int f() { static int n = 0; return ++n; }
constexpr int g(int v) { typedef int I; static_assert(sizeof(I) == 4, ""); return v + f(); }
constexpr int x = g(1);
int main() { return x; }
