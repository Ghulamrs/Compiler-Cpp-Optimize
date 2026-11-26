template <const char *S> struct X {}; int main() { X<"abc"> x; return 0; }
