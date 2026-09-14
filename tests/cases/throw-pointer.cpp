// **A pointer thrown and caught** - [except.handle]/3: the same pointee, a
// more qualified one, `void *`, or a public base of the pointee, a null
// included. A pointer to a fundamental type has the library's `_ZTIPi`;
// one to a class gets a `__pointer_type_info` of its own beside the class's.
extern "C" int printf(const char *, ...);
struct B { int b; B(int v) : b(v) {} };
struct D : B { int d; D(int v) : B(v), d(v + 1) {} };
struct Other { int o; };
int g = 42;
static const char *msg = "hello";
static D dobj(7);
static Other oobj = { 9 };
void thrower(int k) {
    if (k == 1) throw &g;
    if (k == 2) throw msg;
    if (k == 3) throw &dobj;
    if (k == 4) throw (const B *)&dobj;
    if (k == 5) throw &oobj;
    if (k == 6) throw (D *)0;
    if (k == 7) throw (void *)&g;
}
int probe(int k) {
    try { thrower(k); }
    catch (int *p) { printf("int* %d\n", *p); return 1; }
    catch (const char *s) { printf("const char* %s\n", s); return 2; }
    catch (const B *b) { printf("const B* %d\n", b ? b->b : -1); return 3; }
    catch (void *v) { printf("void* %d\n", v == (void *)&g); return 4; }
    return 0;
}
int probeBase(int k) {
    try { thrower(k); }
    catch (B *b) { printf("B* %d %d\n", b->b, (int)((char *)b == (char *)&dobj)); return 1; }
    catch (const void *v) { printf("const void* %d\n", v != 0); return 2; }
    return 0;
}
int main() {
    for (int k = 1; k <= 7; k++) printf("%d -> %d\n", k, probe(k));
    for (int k = 3; k <= 5; k++) printf("%d => %d\n", k, probeBase(k));
    printf("%d => %d\n", 2, probeBase(2));
    return 0;
}
