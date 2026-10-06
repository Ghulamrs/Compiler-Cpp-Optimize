// [basic.scope.pdecl]/1: a name is declared immediately after its complete
// declarator and before its initialiser, so a variable is in scope in its own
// initialiser. Each line below was "'p' was not declared" until 2026-10-06, and
// the shadowing one compiled and wrote the *outer* x where the inner is meant.
extern "C" int printf(const char *, ...);

struct Self {
    Self *me;
    Self(Self *p);
};
Self::Self(Self *p) : me(p) { }

void *p = &p;
int g = sizeof(g);
void *a = &a, *b = &a;
extern "C" { void *selfC = &selfC; }
struct S { static void *m; };
void *S::m = &S::m;
Self fileSelf(&fileSelf);
int arr[3] = { sizeof(arr), 0, 0 };

int x = 5;
int shadow() {
    int x = (x = 3) + 1;     // the inner x, not ::x
    return x;
}

int local() {
    void *q = &q;
    int y = sizeof(y) + 1;
    void *c = &c, *d = &c;
    Self s(&s);
    Self t = &t;
    int n(sizeof(n));
    int ok = q == &q && y == 5 && c == &c && d == &c && s.me == &s &&
             t.me == &t && n == 4;
    if (void *w = &w) ok = ok && w != 0;
    return ok;
}

int main() {
    printf("%d %d %d %d\n", p == &p, g, a == &a && b == &a, selfC == &selfC);
    printf("%d %d %d\n", S::m == &S::m, fileSelf.me == &fileSelf, arr[0]);
    printf("%d %d\n", shadow(), x);
    printf("%d\n", local());
    return 0;
}
