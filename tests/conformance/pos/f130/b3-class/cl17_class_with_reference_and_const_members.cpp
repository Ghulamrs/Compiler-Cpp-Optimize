#include <cstdio>
struct R { int &r; const int c; R(int &x, int y) : r(x), c(y) {} }; struct Arr { const int a[2]; int (&ra)[2]; };
int main() { int x = 1; R r(x, 2); r.r = 5; int arr[2] = {7, 8}; Arr ar{{1, 2}, arr}; ar.ra[0] = 9; std::printf("%d %d %d %d\n", x, r.c, ar.a[1], arr[0]); return 0; }
