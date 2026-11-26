// Trigraphs exist in C++11 ([lex.trigraph]); cl replaces them only with /Zc:trigraphs, clang in C++11 mode does. Both outputs are legitimate; this records which cpp11 chose.
#include <cstdio>
int main() {
    const char *s = "a??=b";
    int n = 0; while (s[n]) ++n;
    std::printf("%d\n", n == 3 ? 1 : 0);
    return 0;
}
