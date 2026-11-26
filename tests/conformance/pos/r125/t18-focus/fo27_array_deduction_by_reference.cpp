// [temp.deduct.call]: a reference parameter deduces the array type itself; the bound is deducible as a non-type parameter.
#include <cstdio>
template <class T> int byRef(T &) { return (int)sizeof(T); }
template <class T, int N> int bound(T (&)[N]) { return N; }
template <class T, int N> const char *spec(const T (&)[N]) { return "T(&)[N]"; }
int main() { int arr[7]; const char s[] = "abc"; std::printf("%d %d %d %s\n", byRef(arr), bound(arr), bound(s), spec(arr)); return 0; }
