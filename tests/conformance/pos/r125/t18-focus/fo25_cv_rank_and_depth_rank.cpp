// [over.ics.rank]/4.3-4.4: void* beats const void* for an int* argument; a derived-to-base conversion to the nearer base wins.
#include <cstdio>
const char *k(void *) { return "void*"; }
const char *k(const void *) { return "const void*"; }
struct G {}; struct H : G {}; struct I : H {};
const char *b(G *) { return "G*"; }
const char *b(H *) { return "H*"; }
const char *r(G &) { return "G&"; }
const char *r(H &) { return "H&"; }
int main() { int i = 0; I x; std::printf("%s %s %s\n", k(&i), b(&x), r(x)); return 0; }
