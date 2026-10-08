// **A temporary made after the full expression ended outlived the function.**
// [stmt.return]/2 copy-initialises the returned object, so `return "<type>";`
// from a function returning a class calls a converting constructor - and that
// constructor's temporary was registered for destruction *after*
// `endFullExpression` had already emptied the list. Nothing emptied it again:
// the entry sat there through the end of the function and into the **next
// function parsed**, which emitted a guarded destructor call for a frame slot
// belonging to a frame that no longer existed.
//
// It is quiet until the stale guard happens to be non-zero, which is why this
// case dirties the frame before calling - on a clean stack the slot reads 0,
// the destructor is skipped, and the wrong code passes. Compiler++'s
// Semantic.cpp has exactly this pair, `describe` ending in `return "<type>";`
// with `countText` beneath it, and on x86_64-linux `free` was handed a pointer
// into the source text about a quarter of the time. macOS never showed it.
//
// Counting live objects rather than constructor calls is CLAUDE.md's rule: a
// stray destructor moves `live` negative, and no elision can do that.
extern "C" int printf(const char *, ...);

int live = 0;

struct Text {
    int tag;
    Text(const char *s);
    Text(const Text &o);
    ~Text();
};

Text::Text(const char *s) : tag(s[0]) { live++; }
Text::Text(const Text &o) : tag(o.tag) { live++; }
Text::~Text() { live--; }

// Ends with a converting return, which is what leaves the temporary behind.
Text describe(int k) {
    if (k == 1) return "int";
    if (k == 2) return "bool";
    return "<type>";
}

// Parsed next, and it was given the stale entry: a guarded destructor for a
// slot of `describe`'s frame.
Text countText(int n) {
    Text t("n");
    if (n > 100) return "big";
    return t;
}

// Writes a pattern over the frame the two above will use, so a stale guard
// reads non-zero rather than the zero a clean stack hands it.
int dirty(int depth) {
    int pad[64];
    for (int i = 0; i < 64; i++) pad[i] = 0x2b2b2b2b + depth;
    if (depth > 0) return pad[0] + dirty(depth - 1);
    return pad[63];
}

// Reads `live` while the argument's temporaries are still alive - one call a statement.
static void show(const char *label, int tag) { printf("%s %d live %d\n", label, tag, live); }

int main() {
    int seed = dirty(3);
    show("describe:", describe(9).tag);
    show("countText:", countText(1).tag);
    show("both:     ", describe(1).tag + countText(2).tag);
    printf("seed used: %d\n", seed != 0);
    return 0;
}
