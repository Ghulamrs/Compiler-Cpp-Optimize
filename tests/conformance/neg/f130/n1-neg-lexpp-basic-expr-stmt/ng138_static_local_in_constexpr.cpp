constexpr int f() { static int x = 1; return x; } int main() { return f(); }
