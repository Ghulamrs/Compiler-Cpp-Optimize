// Character literals: plain, wide, u, U; escapes; multicharacter (implementation-defined, cl: int big-endian bytes); char is signed on x86.
#include <cstdio>
int main() {
    std::printf("%d %d %d %d\n", 'A', '\n', '\x41', '\101');
    std::printf("%d %d\n", (int)sizeof('a'), (int)sizeof(L'a'));
    std::printf("%d %d\n", (int)sizeof(u'a'), (int)sizeof(U'a'));
    std::printf("%u %u\n", (unsigned)u'\u00e9', (unsigned)U'\U0001F600');
    char c = '\xFF';
    std::printf("%d %d\n", c, (int)(unsigned char)c);
    std::printf("%d %d\n", 'ab', '\0');
    wchar_t w = L'\x3b1';
    std::printf("%d\n", (int)w);
    return 0;
}
