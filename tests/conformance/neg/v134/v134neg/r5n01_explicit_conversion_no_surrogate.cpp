typedef int (*FP)(int); int twice(int x) { return x; } struct C { explicit operator FP() const { return twice; } }; int main() { C c; return c(1); }
