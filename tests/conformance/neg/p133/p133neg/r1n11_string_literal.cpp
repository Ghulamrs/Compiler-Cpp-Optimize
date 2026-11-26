template <const char *C> int f() { return C[0]; } int main() { return f<"abc">(); }
