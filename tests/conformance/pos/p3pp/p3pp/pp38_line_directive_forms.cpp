#include <cstdio>
#include <cstring>
#define BASE 100
#line BASE
int a = __LINE__;
#line 200 "renamed.cpp"
int b = __LINE__;
#define FILE_AND_LINE 300 "again.cpp"
#line FILE_AND_LINE
int c = __LINE__;
const char *d = __FILE__;
int main() { std::printf("%d %d %d %s\n", a, b, c, d); return 0; }
