// char16_t and char32_t as types, and the u, U and u8 literals -
// [basic.fundamental]/5, [lex.ccon], [lex.string]. Both are unsigned and 2
// and 4 bytes wide on every target here; the mangling is Ds/Di on Itanium and
// _S/_U on Microsoft, measured from clang for both ABIs. A u literal holds
// UTF-16 code units, a U literal code points, and u8 is a narrow string.
extern "C" int printf(const char *, ...);

int take16(char16_t c) { return c; }
int take32(char32_t c) { return (int)c; }
int pair(const char16_t *p, const char32_t *q) { return p[0] + (int)q[0]; }

int which(int) { return 1; }
int which(unsigned int) { return 2; }

template <class T> int count(const T *s) { int n = 0; while (s[n]) n++; return n; }

int main() {
    printf("%d %d\n", (int)sizeof(char16_t), (int)sizeof(char32_t));
    printf("%d %d\n", char16_t(-1) > 0, char32_t(-1) > 0);
    char16_t c16 = u'x';
    char32_t c32 = U'y';
    printf("%d %d %d %d\n", take16(c16), take32(c32), take16(u'A'), take32(U'B'));
    // [conv.prom]/2: char16_t promotes to int, char32_t to unsigned int.
    printf("%d %d\n", which(c16), which(c32));
    const char16_t *s16 = u"ab";
    const char32_t *s32 = U"cd";
    const char *s8 = u8"ef";
    printf("%d %d %d\n", (int)sizeof(u"ab"), (int)sizeof(U"cd"), (int)sizeof(u8"ef"));
    printf("%d %d %d\n", count(s16), count(s32), count(s8));
    printf("%d %d %d\n", pair(s16, s32), s16[1], (int)s32[1]);
    // A non-ASCII character is one UTF-16 unit, one code point, three bytes.
    const char16_t *e16 = u"é!";
    const char32_t *e32 = U"é!";
    printf("%d %d %d %d\n", count(e16), (int)e16[0], count(e32), (int)e32[0]);
    // Past the BMP: a surrogate pair in u, one unit in U.
    const char16_t *p16 = u"\U0001F600";
    const char32_t *p32 = U"\U0001F600";
    printf("%d %x %x %d %x\n", count(p16), p16[0], p16[1], count(p32), (unsigned)p32[0]);
    // Adjacent literals concatenate and keep the prefix - [lex.string]/13.
    const char16_t *cat = u"ab" "cd";
    printf("%d %d\n", count(cat), cat[3]);
    char16_t arr16[] = u"xyz";
    char32_t arr32[] = U"xyz";
    printf("%d %d\n", (int)sizeof(arr16), (int)sizeof(arr32));
    return 0;
}
