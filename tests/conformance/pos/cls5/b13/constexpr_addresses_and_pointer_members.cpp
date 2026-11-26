// Address constants in constexpr objects ([expr.const]/3): pointers to a global, to a function and to a string
// literal, a literal class whose constexpr constructor stores an address, an aggregate with a pointer member, a
// floating step inside an integral constant expression.
#include <cstdio>

int g = 4;
int twice(int v) { return v * 2; }
struct Ref {
    const int *p;
    int n;
    constexpr Ref(const int *q, int m) : p(q), n(m) {}
};
struct Plain {
    const char *label;
    int value;
};

constexpr int *pg = &g;
constexpr int (*fp)(int) = twice;
constexpr const char *text = "text";
constexpr Ref r(&g, 2);
constexpr Plain plain = { "plain", 3 };
static_assert(r.n == 2 && plain.value == 3, "leaves");
const int CI = 10;

int main() {
    *pg = 5;
    int x = 9;
    switch (x) {
    case (int)(CI * 0.9): std::printf("nine "); break;
    default: std::printf("other ");
    }
    std::printf("%d %d %s %d %d %s %d\n", g, fp(3), text, *r.p, r.p == &g, plain.label, plain.value);
    return 0;
}
