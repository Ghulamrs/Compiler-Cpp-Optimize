// [dcl.align]/2: every alignment of an alignas pack expansion is a power of two, and 3 is not.
template <int... Ns> struct S { alignas(Ns...) char c; };
int main() { S<4, 3> s; (void)s; return 0; }
