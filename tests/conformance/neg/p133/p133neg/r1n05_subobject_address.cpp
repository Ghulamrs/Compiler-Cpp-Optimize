int a[3]; template <int *P> int f() { return *P; } int main() { return f<&a[1]>(); }
