// A raw string literal - [lex.string]/4 and [lex.pptoken]/3: R"delim(...)delim"
// is its characters exactly as written - no escape, no splice, no comment, no
// directive, a newline kept - closed only by the `)delim"` its opening named.
// Every prefix is read: R, u8R, LR, uR and UR. Refused by name until now.
extern "C" int printf(const char *, ...);
extern "C" unsigned long strlen(const char *);

#define SHOW(x) printf("[%s]\n", x)
#define NAME R"(not a macro)"

int main(void) {
    printf("[%s]\n", R"(a\nb\tc)");
    printf("[%s]\n", R"x(quote " and )" inside)x");
    printf("[%s]\n", R"(// not a comment /* nor this */)");
    const char *lines = R"--(first
%:define NOT_A_DIRECTIVE 1
#if not a directive either
last \
line)--";
    printf("[%s] %d\n", lines, (int)strlen(lines));
    SHOW(R"(through a macro, (parens) too)");
    printf("[%s]\n", NAME);
    const char *u8 = u8R"(utf8 \n)";
    printf("[%s] %d\n", u8, (int)(sizeof(LR"(ab)") / sizeof(wchar_t)));
    printf("%d %d\n", (int)sizeof(uR"(ab)"), (int)sizeof(UR"(ab)"));
#ifdef NOT_A_DIRECTIVE
    printf("wrong\n");
#endif
    printf("[%s]\n", R"()");
    return 0;
}
