#include <cstdio>
struct Arr { int (&ra)[2]; }; int arr[2] = {7, 8};
int main() { Arr a{arr}; Arr b = {arr}; std::printf("%d %d\n", a.ra[1], b.ra[0]); return 0; }
