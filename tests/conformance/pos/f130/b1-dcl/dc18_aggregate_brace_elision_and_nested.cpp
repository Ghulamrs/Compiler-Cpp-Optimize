#include <cstdio>
struct In { int a, b; }; struct Out { In i; int c; In j[2]; };
int main() { Out o = {1, 2, 3, 4, 5, 6, 7}; Out p = {{1, 2}, 3, {{4, 5}, {6, 7}}}; Out q = {{1}, 2}; int m[2][3] = {{1}, {4, 5}}; std::printf("%d %d %d %d %d %d %d %d %d %d %d\n", o.i.b, o.c, o.j[1].b, p.j[0].a, q.i.b, q.c, q.j[1].a, m[0][1], m[1][1], m[1][2], (int)sizeof(m) / 4); return 0; }
