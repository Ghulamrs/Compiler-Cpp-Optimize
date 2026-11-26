#include <cstdio>
#include <new>
struct S { int v; S(int v) : v(v) {} ~S() = delete; };
struct Holder { S *p; };
int main() {
    S *p = new S(4);
    Holder h = {p};
    alignas(S) unsigned char raw[sizeof(S)];
    S *q = new (raw) S(5);
    std::printf("%d %d %d\n", h.p->v, q->v, (int)sizeof(S));
    return 0;
}
