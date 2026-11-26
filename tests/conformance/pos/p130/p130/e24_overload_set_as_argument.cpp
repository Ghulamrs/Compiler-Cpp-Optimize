int f(int x) { return x; } int f(double) { return 0; }
int apply(int (*g)(int), int v) { return g(v); }
int main() { return apply(f, 4) == 4 ? 0 : 1; }
