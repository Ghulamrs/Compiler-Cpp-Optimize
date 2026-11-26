template <int *P> struct X {}; int main() { int l = 1; X<&l> x; return 0; }
