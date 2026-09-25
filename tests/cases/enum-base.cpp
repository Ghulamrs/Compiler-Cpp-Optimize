// **An enum-base fixes the underlying type** - [dcl.enum]/5 - and with it
// the enumeration's size, its signedness, the type of each enumerator and
// the overload an enumerator picks. Refused by name until 2026-09-14.
extern "C" int printf(const char *, ...);
enum Small : unsigned char { A = 200, B, C = 255 };
enum Wide : long long { Big = 1LL << 40, Neg = -1 };
enum Sgn : signed char { M = -3, N };
struct P { Small s; char c; Wide w; };
enum Fwd : short;
Small pick(int i) { return i ? B : A; }
int take(Small s) { return s; }
int take(int i) { return i + 1000; }
int main() {
    Small s = C;
    Wide w = Big;
    P p; p.s = A; p.c = 'x'; p.w = Neg;
    printf("%d %d %d %d\n", (int)sizeof(Small), (int)sizeof(Wide), (int)sizeof(Sgn), (int)sizeof(P));
    printf("%d %d %d %lld %d\n", (int)sizeof(A), (int)sizeof(s), (int)sizeof(Fwd), (long long)w, (int)sizeof(Big));
    printf("%d %d %d %d %d\n", A, B, s, M, N);
    printf("%d %d %d %lld %d\n", A + 1, (int)(B + C), (int)p.s, (long long)p.w, pick(1));
    printf("%d %d\n", take(s), take(3));
    unsigned char raw = s; Small back = (Small)raw;
    printf("%d %d %d\n", raw, back, (int)(s == C));
    return 0;
}
