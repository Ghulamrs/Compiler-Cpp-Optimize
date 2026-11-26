template <int *P> int f() { return *P; } int main() { int l = 1; return f<&l>(); }
