// String literals: concatenation, prefixes u8/u/U/L, raw strings with delimiters, sizeof, embedded nul, line splice inside a literal.
#include <cstdio>
#include <cstring>
int main() {
    const char *a = "ab" "cd" "ef";
    std::printf("%s %d\n", a, (int)sizeof("abc"));
    const char *u8 = u8"h\u00e9";
    std::printf("%d %d %d\n", (int)std::strlen(u8), (unsigned char)u8[1], (unsigned char)u8[2]);
    const char16_t *s16 = u"h\u00e9\U0001F600";
    int n16 = 0; while (s16[n16]) ++n16;
    std::printf("%d %u %u %u\n", n16, (unsigned)s16[1], (unsigned)s16[2], (unsigned)s16[3]);
    const char32_t *s32 = U"h\U0001F600";
    int n32 = 0; while (s32[n32]) ++n32;
    std::printf("%d %u\n", n32, (unsigned)s32[1]);
    const char *r = R"(a\nb"c)";
    std::printf("%s %d\n", r, (int)std::strlen(r));
    const char *r2 = R"xyz(")"
)xyz";
    std::printf("[%s] %d\n", r2, (int)std::strlen(r2));
    const char *r3 = R"(line1
line2)";
    std::printf("%d\n", (int)std::strlen(r3));
    const char *nul = "a\0b";
    std::printf("%d %d\n", (int)sizeof("a\0b"), (int)std::strlen(nul));
    const char *spliced = "abc\
def";
    std::printf("%s\n", spliced);
    const wchar_t *w = L"wide" L"r";
    std::printf("%d\n", (int)(sizeof(L"wide") / sizeof(wchar_t)));
    const wchar_t *mix = "plain" L"wide";
    (void)mix; (void)w;
    std::printf("%d\n", (int)sizeof(u8"abc"));
    return 0;
}
