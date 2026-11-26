// Edge rankings: 0 vs nullptr vs NULL; pointer->bool is a conversion (worse than pointer->const void*); derived->base pointer ranks by depth; bool literal.
#include <cstdio>
#include <cstddef>
const char *a(bool) { return "bool"; }
const char *a(const void *) { return "const void*"; }
struct G {}; struct H : G {}; struct I : H {};
const char *b(G *) { return "G*"; }
const char *b(H *) { return "H*"; }
const char *c(int) { return "int"; }
const char *c(char *) { return "char*"; }
const char *d(long) { return "long"; }
const char *d(const char *) { return "const char*"; }
int main() {
    int *ip = 0; I i;
    std::printf("%s %s\n", a(ip), a(true));
    std::printf("%s\n", b((G *)&i));
    std::printf("%s %s %s\n", c(0), c(nullptr), c(NULL == 0));
    std::printf("%s %s\n", d(0L), d("x"));
    return 0;
}
