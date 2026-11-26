#include <cstdio>
#if 1
int a = 1;
#elif 1/0
int a = 2;
#endif
#ifdef __cplusplus
int b = 1;
#elif garbage(((
int b = 2;
#endif
int main() { std::printf("%d %d\n", a, b); return 0; }
