int g = 1; int *const pg = &g; template <int *P> int f() { return *P; } int main() { return f<pg>(); }
