#include <cstdio>
#include <cstring>
#include <cctype>
int main() {
    std::size_t n = 0;
    char buf[16] = "ab";
    std::strncat(buf, "cdefgh", 3);
    char x[16];
    n = std::strxfrm(x, "hello", sizeof x);
    std::printf("%s %d %s %d %d %d\n", buf, (int)n, x, std::strcoll("abc", "abd") < 0, std::strcoll("b", "a") > 0, std::strcoll("q", "q"));
    std::printf("%d %d %d %d\n", std::isblank(' ') != 0, std::isblank('\t') != 0, std::isblank('x') != 0, std::isblank('\n') != 0);
    return 0;
}
