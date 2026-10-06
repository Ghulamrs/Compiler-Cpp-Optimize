// 64-bit shifts by a constant, each count across the word boundary, signed and unsigned, both ways;
// at -O1 and up the C6000 moves or splices the pair in place where a variable count branches.
extern "C" int printf(const char *, ...);

static void show(const char *what, unsigned long long v)
{
    printf("%s %08x%08x\n", what, (unsigned)(v >> 32), (unsigned)v);
}

int main()
{
    unsigned long long us[] = { 0x8123456789abcdefull, 1ull, 0xffffffffffffffffull };
    long long ss[] = { -0x123456789abcdefll, 0x7edcba9876543210ll, -1ll };
    for (int i = 0; i < 3; i++) {
        unsigned long long u = us[i];
        long long s = ss[i];
        show("u<<0 ", u << 0);   show("u<<1 ", u << 1);   show("u<<31", u << 31);
        show("u<<32", u << 32);  show("u<<33", u << 33);  show("u<<63", u << 63);
        show("u>>0 ", u >> 0);   show("u>>1 ", u >> 1);   show("u>>31", u >> 31);
        show("u>>32", u >> 32);  show("u>>33", u >> 33);  show("u>>63", u >> 63);
        show("s>>1 ", (unsigned long long)(s >> 1));   show("s>>31", (unsigned long long)(s >> 31));
        show("s>>32", (unsigned long long)(s >> 32));  show("s>>40", (unsigned long long)(s >> 40));
        show("s>>63", (unsigned long long)(s >> 63));
        show("s<<4 ", (unsigned long long)(s & 0xfffffffffffll) << 4);
        unsigned long long k = u >> 8 << 16 >> 24;      // three in a row
        show("chain", k);
        int n = 5;
        show("var  ", u << (n + 27));                   // a count that is not a constant
    }
    return 0;
}
