// **A member pointer is sized by the target, not by the ABI's name alone.**
// Itanium's data-member pointer is a ptrdiff_t and its function one a pair
// of them - eight and sixteen bytes on the 64-bit targets, four and eight on
// the C6000. They were 8 and 16 everywhere, so a class holding one was laid
// out wrong on tms6747 (Fable 5.1's review, A2). In pointer widths, so that
// one answer serves every target.
extern "C" int printf(const char *, ...);
struct S { int a, b; int get() { return b; } int twice() { return 2 * a; } };
struct H { int (S::*f)(); int S::*d; char tag; };
int main() {
    int S::*pm = &S::b;
    int (S::*pf)() = &S::twice;
    S s; s.a = 3; s.b = 4;
    H h; h.f = &S::get; h.d = &S::a; h.tag = 'x';
    // Itanium: a ptrdiff_t and a pair of them. Microsoft, single inheritance:
    // an int and one code pointer - measured with cl, 4 and 8 on x64.
#ifdef _WIN32
    const int wantPm = 4, wantPf = 8;
#else
    const int wantPm = (int)sizeof(void *), wantPf = 2 * (int)sizeof(void *);
#endif
    printf("%d %d %d %d %c\n", s.*pm, (s.*pf)(), (s.*h.f)(), s.*h.d, h.tag);
    printf("%d %d %d\n", (int)sizeof(pm) == wantPm, (int)sizeof(pf) == wantPf,
           (int)sizeof(H) == 2 * wantPf);
    return 0;
}
