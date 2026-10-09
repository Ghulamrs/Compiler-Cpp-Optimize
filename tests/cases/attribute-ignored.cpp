// [dcl.attr.grammar]/5: an attribute-token the implementation does not recognise is ignored, and
// an attribute may stand wherever the grammar lets one - before the decl-specifiers, at the start
// of a declarator, after a `*` or `&`, after the declarator-id, after an array bound, after a
// parameter list, on a class or enum head, on an alias's name, on a parameter, and on its own as
// an attribute-declaration. C++11's own two are read and change nothing. clang (-Wunknown-attributes
// is a warning, not an error under -pedantic-errors) prints the same lines.
extern "C" int printf(const char *, ...);
extern "C" void exit(int);

[[vendor::tag]] int a = 1;
int [[tag]] b = 2;
int * [[tag]] p = &a;
int c [[tag]] = 3;
int arr[2] [[tag]] = { 4, 5 };
int twice(int n) [[vendor::tag(1, "s")]];
int twice(int n) { return n * 2; }
[[noreturn]] void die(int code) { exit(code); }
void dep(int n [[carries_dependency]]) { printf("%d\n", n); }
struct [[tag]] S { int v [[tag]]; int get() const [[tag]] { return v; } };
enum [[vendor::tag]] E { Red = 10, Green };
using Alias [[tag]] = long;
void param([[tag]] int q, int & [[tag]] r) { r = q; }
[[tag]];

int main() {
    [[tag]] int local = 6;
    S s;
    s.v = 7;
    Alias l = 8;
    int out = 0;
    param(9, out);
    printf("%d %d %d %d %d %d\n", a, b, *p, c, arr[0], arr[1]);
    int t = twice(local);
    printf("%d %d %d %d %ld %d\n", t, s.get(), Green, out, l, (int)sizeof(arr));
    dep(11);
    if (a == 0) die(1);
    return 0;
}
