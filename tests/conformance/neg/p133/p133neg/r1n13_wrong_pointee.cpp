long g = 1; template <int *P> int f() { return *P; } int main() { return f<&g>(); }
