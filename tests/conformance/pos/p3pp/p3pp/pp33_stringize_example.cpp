#include <cstdio>
#include <cstring>
#define str(s) # s
#define xstr(s) str(s)
#define debug(s, t) std::printf("x" # s "= %d, x" # t "= %s\n", \
    x ## s, x ## t)
#define glue(a, b) a ## b
#define xglue(a, b) glue(a, b)
#define HIGHLOW "hello"
#define LOW LOW ", world"
int main() {
    int x1 = 7;
    const char *x2 = "two";
    debug(1, 2);
    std::printf("%s\n", str(strncmp("abc\0d", "abc", '\4') // this goes away
        == 0) str(: @\n));
    std::printf("%s|%s|%s\n", glue(HIGH, LOW), xglue(HIGH, LOW), xstr(glue(HIGH, LOW)));
    return 0;
}
