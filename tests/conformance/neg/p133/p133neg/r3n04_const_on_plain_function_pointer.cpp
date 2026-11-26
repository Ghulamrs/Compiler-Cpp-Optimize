int f() { return 1; } int main() { int (*p)() const = f; return p(); }
