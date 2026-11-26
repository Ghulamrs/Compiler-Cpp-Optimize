// [class.mi]/4, [class.derived]: a polymorphic base written after a plain one; members of both, conversions both ways
#include <cstdio>
struct E { int e; E() : e(5) {} };
struct N { virtual int n() const { return 1; } };
struct Y : E, N { int y; Y() : y(7) {} int n() const { return e + y; } };
int main() {
    Y y; E *pe = &y; N *pn = &y;
    Y *back = static_cast<Y *>(pn);
    std::printf("%d %d %d %d %d\n", pn->n(), pe->e, back->y, (int)sizeof(Y), (int)((char *)pe - (char *)&y));
    return 0;
}
