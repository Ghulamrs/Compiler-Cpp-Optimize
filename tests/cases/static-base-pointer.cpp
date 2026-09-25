// A derived object's address converted to a base pointer in a constant
// initialiser moves to the base subobject: `V3 *v43 = &v4;` with V3 the
// second base is `v4 + 8` (16 on a 64-bit host), as clang and cl6x write it.
// cxx1 folded the address and dropped the adjustment, so the pointer named
// the V1 subobject and `v43->h()` called V1::f through the wrong vtable -
// the third review's layout probe, seen from the C6000 and true everywhere.
extern "C" int printf(const char *, ...);
struct V1 { int a; virtual int f() { return 1; } };
struct V3 { int c; virtual int h() { return 3; } };
struct V4 : V1, V3 { int d; int h() { return 4; } };
V4 v4;
V3 *v43 = &v4;
static V3 *const inner = &v4;
int main() {
    V3 *local = &v4;
    printf("%d %d %d %d\n", v43->h(), local->h(), (int)(v43 == local), inner->h());
    return 0;
}
