#include <cstdio>
template <int N> int len(const char (&)[N]) { return N; }
int main() { const char (&r)[4] = "abc"; char a[] = "ab"; const char *p = "x" "y"; std::printf("%d %d %d %s\n", len("hello"), (int)sizeof(r), (int)sizeof(a), p); return 0; }
