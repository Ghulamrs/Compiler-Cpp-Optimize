#include <cstdio>
int gv = 5; void fn() { std::printf("fn "); } struct S { int m = 3; int f() { return 4; } }; extern const char str[]; const char str[] = "s"; int arr[3] = {1, 2, 3};
template <int *P> int deref() { return *P; } template <void (*F)()> void call() { F(); } template <int S::*PM> int member(S &s) { return s.*PM; } template <int (S::*PF)()> int mcall(S &s) { return (s.*PF)(); } template <const char *C> int first() { return C[0]; } template <int &R> void inc() { R++; } template <int (&A)[3]> int second() { return A[1]; }
int main() { S s; call<fn>(); call<&fn>(); inc<gv>(); std::printf("%d %d %d %c %d %d\n", deref<&gv>(), member<&S::m>(s), mcall<&S::f>(s), first<str>(), second<arr>(), gv); return 0; }
