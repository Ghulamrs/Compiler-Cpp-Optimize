// A label before a declaration: a declaration is a statement in C++ (the cl
// review's A10 - the refusal was C's rule). A case label may stand before one
// too, as long as no later label jumps past its initialiser ([stmt.dcl]/3).
extern "C" int printf(const char *, ...);
int main() {
    int j = 0;
    for (;;) { if (j == 3) goto out; ++j; }
out:
    int r = j * 2;
    switch (r) {
    case 6:
        int k = r + 1;
        printf("%d %d\n", r, k);
    }
    return 0;
}
