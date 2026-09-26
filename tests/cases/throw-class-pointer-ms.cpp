// A class and a pointer thrown and caught on every target - written for the
// Microsoft ThrowInfo chain, which until 2026-09-26 reached a fundamental type
// only. Each shape here is one record cl writes: a class with a base (one
// catchable per class in the _CTA, the base's at its offset, the copy
// constructor named in each), a plain class (no copy constructor, `0`), a
// pointer (the pointer, its base pointers and `void *`, all "simple"), a
// pointer to const (`_TI2C` as cl writes it, the const off every name and 1 in the attributes),
// a base caught by reference (adjectives 8), a class caught by value (the
// runtime copies it and the handler destroys it before it returns), and a
// catch-all. No handler returns, and no destructible local sits beside a
// try - both are still refused for x86_64-windows and are their own steps.
// Measured from clang -target x86_64-pc-windows-msvc on 2026-09-26; the box
// runs it through run-cases.cmd.
extern "C" int printf(const char *, ...);

static int live = 0;
struct Base {
    int b;
    Base(int x);
    Base(const Base &o);
    ~Base();
};
Base::Base(int x) : b(x) { live++; }
Base::Base(const Base &o) : b(o.b) { live++; }
Base::~Base() { live--; }
struct E : Base {
    int v;
    E(int x);
    E(const E &o);
    ~E();
};
E::E(int x) : Base(x), v(x * 2) {}
E::E(const E &o) : Base(o), v(o.v) {}
E::~E() {}
struct Plain { int p; };

static void throwE(int x) { throw E(x); }
static void throwPlain() { Plain p = { 3 }; throw p; }
static void throwPtr(int *p) { throw p; }
static void throwCPtr(const char *s) { throw s; }
static E ge(6);

static int catchByRef() { int r = -1; try { throwE(4); } catch (const Base &b) { r = b.b; } return r; }
static int catchByValue() { int r = -1; try { throwE(5); } catch (E e) { r = e.v; } return r; }
static int catchBaseByValue() { int r = -1; try { throwE(7); } catch (Base b) { r = b.b; } return r; }
static int catchPlain() { int r = -1; try { throwPlain(); } catch (Plain q) { r = q.p; } return r; }
static int catchPtr() { int n = 9, r = -1; try { throwPtr(&n); } catch (int *p) { r = *p; } return r; }
static int catchCPtr() { int r = -1; try { throwCPtr("hi"); } catch (const char *s) { r = s[1]; } return r; }
static int catchBasePtr() { int r = -1; try { throw &ge; } catch (Base *b) { r = b->b; } return r; }
static int catchVoidPtr() { int r = -1; try { throw &ge; } catch (void *) { r = 11; } return r; }
static int catchMissed() {
    int r = -1;
    try { throwPtr(0); } catch (const char *) { r = 1; } catch (int *p) { r = p == 0 ? 2 : 3; }
    return r;
}
static int catchAll() { int r = -1; try { throwE(1); } catch (...) { r = 12; } return r; }

int main() {
    printf("%d %d %d %d\n", catchByRef(), catchByValue(), catchBaseByValue(), catchPlain());
    printf("%d %d %d %d %d %d\n", catchPtr(), catchCPtr(), catchBasePtr(), catchVoidPtr(),
           catchMissed(), catchAll());
    // Every thrown copy the runtime built it destroyed again, on both ABIs; the
    // one object alive is `ge`. How many copies were made is elision's to choose.
    printf("live %d\n", live);
    return 0;
}
