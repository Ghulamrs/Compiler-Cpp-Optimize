#include <cstdio>
template <class T> struct B {}; struct D : B<int> {}; template <class T> int fromBase(B<T>) { return (int)sizeof(T); } template <class T> int fromBasePtr(const B<T> *) { return (int)sizeof(T) * 10; } template <class T, int N> int sized(const T (&)[N]) { return N; } template <class T> int qual(const T *) { return 1; } template <class T> int fnp(T (*)(T)) { return 2; } int same(int x) { return x; }
int main() { D d; int *p = nullptr; std::printf("%d %d %d %d %d\n", fromBase(d), fromBasePtr(&d), sized("hello"), qual(p), fnp(same)); return 0; }
