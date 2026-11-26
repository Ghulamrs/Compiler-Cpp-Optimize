#include <cstdio>
#include <initializer_list>
template <class T, int N> int arr(T (&)[N]) { return N; } template <class R, class A> int fn(R (*)(A)) { return 1; } template <class C, class M> int mem(M C::*) { return 2; } template <class T> int il(std::initializer_list<T>) { return (int)sizeof(T); } template <class T> int cref(const T &) { return (int)sizeof(T); } template <class T> int ptr(T *) { return (int)sizeof(T); } template <template <class> class TT, class T> int tt(TT<T>) { return 3; } template <class T, class U> int two(T, U) { return 4; } template <class T> int def(T = T()) { return 5; }
template <class T> struct Box { T t; }; struct S { int m; }; int twice(int x) { return x; } int ar5[5];
int main() { Box<int> b{1}; int x = 1; const int cx = 2; std::printf("%d %d %d %d %d %d %d %d %d %d\n", arr(ar5), fn(twice), mem(&S::m), il({1, 2}), cref(ar5), cref("abc"), ptr(&cx), tt(b), two(x, cx), def<int>()); return 0; }
