// At -O2 for the C6000, value numbering removed the remake of p's address in the arm of
// `limb + 2 < 8 ? p[limb + 2] : 0` and then took the register holding it for the index.
extern "C" int printf(const char *, ...);

struct Power { unsigned limbs[4]; int k; int exact; };

static unsigned long long fives[28];
static Power powers[25];

static int bitLength(unsigned x)
{
    int n = 0;
    if (x >> 16) { n += 16; x >>= 16; }
    if (x >> 8) { n += 8; x >>= 8; }
    if (x >> 4) { n += 4; x >>= 4; }
    if (x >> 2) { n += 2; x >>= 2; }
    if (x >> 1) { n += 1; x >>= 1; }
    return n + (int)x;
}

// The top 64 bits of w 5^b C, C the power's four limbs, and whether anything below them is set.
static bool topBits(unsigned long long w, int e, unsigned long long &bits, unsigned &sticky)
{
    if (e < -364 || e > 335 || w == 0) return false;
    int a = (e + 364) / 28, b = e + 364 - a * 28;
    const Power &c = powers[a];
    unsigned wv[2] = { (unsigned)w, (unsigned)(w >> 32) };
    unsigned fv[2] = { (unsigned)fives[b], (unsigned)(fives[b] >> 32) };
    unsigned x[4] = { 0, 0, 0, 0 }, p[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 2; i++) {
        unsigned carry = 0;
        for (int j = 0; j < 2; j++) {
            unsigned long long t = (unsigned long long)wv[i] * fv[j] + x[i + j] + carry;
            x[i + j] = (unsigned)t;
            carry = (unsigned)(t >> 32);
        }
        x[i + 2] = carry;
    }
    for (int i = 0; i < 4; i++) {
        unsigned carry = 0;
        for (int j = 0; j < 4; j++) {
            unsigned long long t = (unsigned long long)x[i] * c.limbs[j] + p[i + j] + carry;
            p[i + j] = (unsigned)t;
            carry = (unsigned)(t >> 32);
        }
        p[i + 4] = carry;
    }
    int top = 7;
    while (p[top] == 0) top--;
    int length = top * 32 + bitLength(p[top]), from = length - 64, limb = from >> 5, shift = from & 31;
    unsigned lo = p[limb], mid = p[limb + 1], hi = limb + 2 < 8 ? p[limb + 2] : 0;
    unsigned st = shift ? lo << (32 - shift) : 0;
    for (int i = 0; i < limb; i++) st |= p[i];
    unsigned s0 = shift ? (lo >> shift) | (mid << (32 - shift)) : lo;
    unsigned s1 = shift ? (mid >> shift) | (hi << (32 - shift)) : mid;
    unsigned last = s0 & 0x7FF;
    if (!c.exact && (last == 0x3FF || last == 0x7FF)) return false;
    int exponent = from + c.k + e, biased = exponent + 63 + 1023;
    if (biased < 1 || biased > 2045) return false;
    bits = (unsigned long long)s1 << 32 | s0;
    sticky = st;
    return true;
}

int main()
{
    unsigned long long f = 1;
    for (int i = 0; i < 28; i++) { fives[i] = f; f *= 5; }
    for (int i = 0; i < 25; i++) {
        powers[i].limbs[0] = 0x89abcdefu + i * 0x1111u;
        powers[i].limbs[1] = 0x12345678u * (i + 1);
        powers[i].limbs[2] = 0xdeadbeefu ^ (i * 77u);
        powers[i].limbs[3] = 0x80000000u + i * 0x01234567u;
        powers[i].k = -100 + i * 65;
        powers[i].exact = i & 1;
    }
    unsigned long long ws[] = { 1ull, 12345ull, 0xffffffffffffull, 987654321987654321ull, 3ull };
    int es[] = { -300, -100, -17, 0, 5, 50, 200, 300 };
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 8; j++) {
            unsigned long long bits = 0;
            unsigned sticky = 0;
            bool ok = topBits(ws[i], es[j], bits, sticky);
            printf("%d %08x%08x %08x\n", ok, (unsigned)(bits >> 32), (unsigned)bits, sticky);
        }
    return 0;
}
