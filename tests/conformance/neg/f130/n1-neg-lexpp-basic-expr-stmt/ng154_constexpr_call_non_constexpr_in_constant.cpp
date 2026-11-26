int g() { return 1; } constexpr int f() { return g(); } int main() { constexpr int x = f(); return x; }
