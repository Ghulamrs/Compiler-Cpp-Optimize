// The eleven word-spelled alternative tokens of [lex.digraph] table 2.
//
// **They are keywords in C++, not the macros <iso646.h> makes of them in C**,
// and [lex.digraph]/2 says each behaves in every respect as the operator it
// spells, spelling apart. So the whole feature is a rewrite in the lexer: a
// word that is one of the eleven becomes its primary token there, and no rule
// downstream - expression, declarator, destructor name, `operator` name -
// needs to have heard of it.
//
// That is why this case reaches past expressions. `bitand` is `&`, so it is a
// reference declarator and an address-of as well as a bitwise and; `compl` is
// `~`, so it names a destructor. Neither is a special case in the parser and
// neither would work if the eleven had been given rules of their own.
//
// Nine of the eleven used to be refused with `expected ';'` or `expected ')'`
// - a message naming no feature, which `tools/exclusions` cannot list and so
// nobody could see was missing. `not` and `compl` were refused by name because
// they can begin an expression, which is the only reason those two were
// visible at all.
//
// Two shapes here are for names.sh and not for the feature. `S`'s constructor
// and destructor are defined out of line, because clang emits only the C2
// variant of one written inline; and the block holding the `S` calls printf
// after it, because a scope whose only cleanup can never be reached needs no
// landing pad, and clang drops the one cxx1 keeps. Neither is about the
// alternative tokens, and removing either turns this case red for a reason
// that is not.
//
// The preprocessor gets them too, and separately: `#if 1 and 1` is a
// conditional-expression in the same grammar, and that evaluator works on text
// rather than on the lexer's tokens. It rewrites the words after macro
// expansion, because a macro *body* may spell one where a macro *name* may
// not.

extern "C" int printf(const char *, ...);

#define BOTH(a, b) ((a) and (b))

// A name that merely contains one of the eleven is a name.
static int android = 3;
static int not_a_word = 4;

// Defined out of line on purpose: clang emits only the C2 variant of a
// constructor written inline, and names.sh would report that as a mangling
// difference. `compl S` is the destructor's name either way.
struct S {
    int v;
    S(int x);
    compl S();
};

S::S(int x) : v(x) { }
S::compl S() { printf("~S %d\n", v); }

static int twice(int bitand r) { r *= 2; return r; }

int main() {
    int a = 12, b = 10;
    bool p = true, q = false;

    if (a > 0 and b > 0) printf("and\n");
    if (p or q) printf("or\n");
    if (not q) printf("not\n");
    if (p xor q) printf("xor\n");
    if (a not_eq b) printf("not_eq\n");

    printf("%d %d %d %d\n", a bitand b, a bitor b, a xor b, compl a);

    int c = a;
    c and_eq 6;  printf("%d\n", c);
    c or_eq 1;   printf("%d\n", c);
    c xor_eq 3;  printf("%d\n", c);

    int bitand r = a;
    r = 7;
    int before = a;
    int doubled = twice(a);
    printf("%d %d\n", before, doubled);

    int *ptr = bitand a;
    printf("%d\n", *ptr);

    printf("%d %d %d\n", BOTH(1, 1), android, not_a_word);

    { S s(5); printf("v %d\n", s.v); }

#if 1 and 1
    printf("pp and\n");
#endif
#if 0 or 2 bitand 2
    printf("pp or bitand\n");
#endif
#if not 0 and 'a' == 97
    printf("pp not\n");
#endif
    return 0;
}
