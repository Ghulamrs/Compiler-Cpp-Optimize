// `int x{5};` and `int x = {5};` on a member are the member initialiser `int x = 5;` already is -
// [class.mem]/4, read where [dcl.init.list] reads braces: one value, which may not narrow
// (member-brace-narrowing-refused). `{}` and `{0}` still zero; a constructor that names the member
// in its own list wins, [class.base.init]/9. clang prints the same lines.
extern "C" int printf(const char *, ...);

enum Mode { Slow = 2, Fast = 9 };

struct P {
    int a{5};
    int b = {6};
    double d{2.5};
    char c{'x'};
    long long w{1 + 2};
    Mode m{Fast};
    unsigned u{};
    int z = {0};
    int from{a + 1};
    P() {}
    P(int v) : a(v) {}
};

struct Q : P { int k{100 - 1}; };

int main() {
    P p;
    P q(50);
    printf("%d %d %.1f %c %lld %d %u %d %d\n", p.a, p.b, p.d, p.c, p.w, (int)p.m, p.u, p.z, p.from);
    printf("%d %d %d\n", q.a, q.b, q.from);
    Q r;
    printf("%d %d\n", r.a, r.k);
    return 0;
}
