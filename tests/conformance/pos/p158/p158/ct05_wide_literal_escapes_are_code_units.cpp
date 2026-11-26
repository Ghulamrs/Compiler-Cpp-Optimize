#include <cstdio>
#include <cstdlib>
#include <cwchar>

// [lex.string]/10: in a wide, char16_t or char32_t literal a hexadecimal or octal escape is one code unit of
// its value; in an ordinary literal one byte.
int main() {
    const wchar_t *w = L"A\x00e9\x20ac\101";
    const char16_t u[] = u"\x263a\xD7FF";
    const char32_t U[] = U"\x1F600\x7f";
    const char n[] = "\x41\101\xe9";
    std::printf("%d:", (int)std::wcslen(w));
    for (int i = 0; w[i]; i++) std::printf(" %x", (unsigned)w[i]);
    std::printf(" | %x %x %d | %x %x %d", (unsigned)u[0], (unsigned)u[1], (int)(sizeof u / sizeof u[0]), (unsigned)U[0],
                (unsigned)U[1], (int)(sizeof U / sizeof U[0]));
    std::printf(" | %d %x\n", (int)sizeof n, (unsigned)(unsigned char)n[2]);
    std::printf("%d\n", (int)(MB_CUR_MAX >= 1));
    return 0;
}
