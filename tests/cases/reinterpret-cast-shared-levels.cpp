// [expr.const.cast]/8: reinterpret_cast casts away constness only over the levels the
// two pointer types share, and never by the pointer value's own top-level const.
extern "C" int printf(const char *, ...);

// The operand is itself const: that const is the value's and stays behind.
unsigned word(const char *const *const w) { return reinterpret_cast<const unsigned *>(w)[1]; }
unsigned wordNoTop(const char *const *w) { return reinterpret_cast<const unsigned *>(w)[1]; }

// The target's one level is const, so a const one level further down is not in question.
const unsigned *fromPtrConst(int *const *p) { return reinterpret_cast<const unsigned *>(p); }
const int *fromPlain(char **p) { return reinterpret_cast<const int *>(p); }

// Adding const at every level from the top is a qualification conversion.
const char *const *addAll(char **p) { return reinterpret_cast<const char *const *>(p); }

// A reference: the object's own const is the first level and is kept.
const unsigned &asWord(const int &n) { return reinterpret_cast<const unsigned &>(n); }

int main() {
    unsigned cells[3] = { 11, 22, 33 };
    const char *const *w = reinterpret_cast<const char *const *>(cells);
    printf("%u %u\n", word(w), wordNoTop(w));
    int *ptrs[2];
    ptrs[0] = ptrs[1] = 0;
    printf("%d %d\n", fromPtrConst(ptrs) == reinterpret_cast<const unsigned *>(ptrs),
           fromPlain(reinterpret_cast<char **>(ptrs)) == reinterpret_cast<const int *>(ptrs));
    char text[] = "abc";
    char *names[1] = { text };
    printf("%s\n", addAll(names)[0]);
    const int n = 7;
    printf("%u\n", asWord(n));
    return 0;
}
