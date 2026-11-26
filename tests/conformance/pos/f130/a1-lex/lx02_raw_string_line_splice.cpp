#include <cstdio>
int main() { const char *s = R"(a\
b)"; std::printf("%d %c\n", (int)sizeof(R"(a\
b)"), s[1]); return 0; }
