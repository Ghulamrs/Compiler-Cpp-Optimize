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
    const int w = (int)sizeof(void *);
    printf("%d %d %d %d %c\n", s.*pm, (s.*pf)(), (s.*h.f)(), s.*h.d, h.tag);
    printf("%d %d %d\n", (int)sizeof(pm) / w, (int)sizeof(pf) / w, (int)sizeof(H) / w);
    return 0;
}
