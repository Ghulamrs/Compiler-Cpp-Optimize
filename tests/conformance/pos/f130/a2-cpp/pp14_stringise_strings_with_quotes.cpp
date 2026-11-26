#include <cstdio>
#define S(x) #x
int main() { std::printf("%s|%s|%s\n", S("q\"q"), S('\''), S(a "b" c)); return 0; }
