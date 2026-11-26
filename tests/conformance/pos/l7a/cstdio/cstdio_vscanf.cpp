#include <cstdio>
#include <cstdarg>
static int readString(const char *from, const char *how, ...) {
    va_list ap;
    va_start(ap, how);
    int got = std::vsscanf(from, how, ap);
    va_end(ap);
    return got;
}
static int readStdin(const char *how, ...) {
    va_list ap;
    va_start(ap, how);
    int got = std::vscanf(how, ap);
    va_end(ap);
    return got;
}
static int readFile(std::FILE *f, const char *how, ...) {
    va_list ap;
    va_start(ap, how);
    int got = std::vfscanf(f, how, ap);
    va_end(ap);
    return got;
}
int main() {
    int a = 0, b = 0;
    char w[8];
    int got = readString("12 34 xy", "%d %d %7s", &a, &b, w);
    int none = 7;
    int fromStdin = readStdin("%d", &none);
    std::FILE *f = std::tmpfile();
    std::fputs("-5 6.5", f);
    std::rewind(f);
    int i = 0;
    double d = 0;
    int fromFile = readFile(f, "%d %lf", &i, &d);
    std::fclose(f);
    std::printf("%d %d %d %s %d %d %d %d %g\n", got, a, b, w, fromStdin, none, fromFile, i, d);
    return 0;
}
