// The bit-field and the const member path of op08 split out: postfix ++ on a bit-field, and compound assignment on one.
#include <cstdio>
struct BF { unsigned a : 4; int s : 3; };
int main() { BF b; b.a = 14; b.s = 2; b.a += 1; b.s -= 1; ++b.a; std::printf("%u %d\n", b.a, b.s); return 0; }
