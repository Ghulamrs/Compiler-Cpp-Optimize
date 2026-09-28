/* Recursion: a call, a compare and two adds, 46,000 times over. */
#include <stdio.h>
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main(void) { printf("fib %d\n", fib(22)); return 0; }
