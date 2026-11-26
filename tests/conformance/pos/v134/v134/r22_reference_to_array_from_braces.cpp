#include <cstdio>
int main() { const int (&cr)[3] = {4, 5, 6}; int (&&rr)[2] = {7, 8}; rr[0] += cr[2]; std::printf("%d %d %d\n", cr[0], rr[0], rr[1]); return 0; }
