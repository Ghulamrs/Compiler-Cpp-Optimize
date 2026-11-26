#include <cstdio>
struct Pod { int a; int b; }; struct Ctor { int a; Ctor() {} }; struct Mixed { int a; Pod p; Ctor c; };
int main() { Pod *p = new Pod(); Pod q = Pod(); Pod r{}; Mixed *m = new Mixed(); int i = int(); Pod arr[2] = {}; static Pod sp; char c = char();
std::printf("%d %d %d %d %d %d %d %d %d\n", p->a + p->b, q.a, r.b, m->a, m->p.b, i, arr[1].a, sp.a, (int)c); delete p; delete m; return 0; }
