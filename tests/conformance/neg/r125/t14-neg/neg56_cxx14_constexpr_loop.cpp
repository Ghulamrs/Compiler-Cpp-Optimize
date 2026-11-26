constexpr int sum(int n) { int s = 0; for (int i = 0; i <= n; ++i) s += i; return s; }
int main() { static_assert(sum(3) == 6, ""); return 0; }
