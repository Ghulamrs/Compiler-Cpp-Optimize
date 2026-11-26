#include <cstdio>
template <class T> int sz(T) { return (int)sizeof(T); } struct { int a, b; } anon_s;
int main() { enum { e } x; std::printf("%d %d\n", sz(anon_s), sz(x)); return 0; }
