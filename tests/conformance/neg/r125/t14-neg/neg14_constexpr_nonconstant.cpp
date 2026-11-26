constexpr int sq(int x) { return x * x; }
int main() { int n = 3; constexpr int c = sq(n); return c; }
