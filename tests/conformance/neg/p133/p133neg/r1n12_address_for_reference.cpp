int g = 1; template <int &R> int f() { return R; } int main() { return f<&g>(); }
