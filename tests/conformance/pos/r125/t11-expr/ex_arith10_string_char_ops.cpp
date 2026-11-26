// Character handling with <cctype> and the sign of char, string literal addresses stability, char arrays vs pointers, strcmp/strlen/memcpy from the shipped C headers, snprintf formatting of widths and flags.
#include <cstdio>
#include <cstring>
#include <cctype>
int main() {
    char buf[32]; std::snprintf(buf, sizeof buf, "[%5d|%-5d|%05d|%+d|%x|%o|%#x]", 42, 42, 42, 42, 255, 8, 255); std::printf("%s\n", buf);
    std::snprintf(buf, sizeof buf, "[%8.3f|%-8.2e|%g]", 3.14159, 31415.9, 0.0001); std::printf("%s\n", buf);
    std::snprintf(buf, sizeof buf, "[%s|%10s|%-10s|%.2s]", "ab", "ab", "ab", "abcdef"); std::printf("%s\n", buf);
    std::snprintf(buf, sizeof buf, "[%c|%%|%p]", 'z', (void *)0); std::printf("%s\n", buf);
    char s[] = "Hello, World"; int up = 0, lo = 0, sp = 0; for (char *p = s; *p; ++p) { if (std::isupper((unsigned char)*p)) ++up; if (std::islower((unsigned char)*p)) ++lo; if (std::isspace((unsigned char)*p)) ++sp; }
    std::printf("%d %d %d %d\n", up, lo, sp, (int)std::strlen(s));
    std::printf("%d %d %d\n", std::strcmp("a", "b") < 0, std::strcmp("b", "a") > 0, std::strcmp("x", "x"));
    char dst[16]; std::memcpy(dst, s, 5); dst[5] = 0; std::printf("%s %s\n", dst, std::strchr(s, 'W'));
    std::memset(dst, 'x', 3); dst[3] = 0; std::printf("%s %d\n", dst, (int)std::toupper('q'));
    const char *a = "same"; const char *b = "same"; std::printf("%d\n", std::strcmp(a, b) == 0);
    std::printf("%d %d\n", std::strncmp("abcdef", "abcxyz", 3), std::strncmp("abcdef", "abcxyz", 4) < 0);
    char c = 'A'; c += 32; std::printf("%c %d\n", c, c - 'a');
    std::printf("%d %d\n", std::isdigit('5') != 0, std::isalpha('5') != 0);
    return 0;
}
