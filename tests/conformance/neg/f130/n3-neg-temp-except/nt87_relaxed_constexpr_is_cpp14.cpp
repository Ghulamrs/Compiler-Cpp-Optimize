constexpr int f(int n) { int r = 1; while (n > 1) r *= n--; return r; } int main() { return f(3); }
