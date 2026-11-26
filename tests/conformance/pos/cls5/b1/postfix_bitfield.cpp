#include <cstdio>
struct F { unsigned u : 3; int s : 4; bool b : 1; unsigned rest : 8; };
int main() {
    F f = {6, -7, false, 200};
    unsigned a = f.u++;
    unsigned b = f.u++;
    int c = f.s--;
    int d = f.s--;
    bool e = f.b++;
    F *p = &f;
    unsigned g = p->rest--;
    std::printf("%u %u %u %d %d %d %d %d %u %u\n", a, b, (unsigned)f.u, c, d, (int)f.s, (int)e, (int)f.b, g, (unsigned)f.rest);
    return 0;
}
