/* FNV-1a over a rebuilt buffer: byte loads, xor and a 32-bit multiply. */
#include <stdio.h>
static unsigned hashes(int rounds)
{
    char buf[64];
    unsigned h = 0;
    int r, i;
    for (r = 0; r < rounds; ++r) {
        const char *p;
        unsigned x = 2166136261u;
        for (i = 0; i < 63; ++i) buf[i] = (char)('a' + (r + i) % 26);
        buf[63] = 0;
        for (p = buf; *p; ++p) x = (x ^ (unsigned char)*p) * 16777619u;
        h ^= x;
    }
    return h;
}
int main(void) { printf("hash %u\n", hashes(2000)); return 0; }
