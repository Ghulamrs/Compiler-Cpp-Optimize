// wchar_t on the four targets: 4 bytes and signed on x86_64-linux and
// arm64-darwin, 2 bytes and unsigned on x86_64-windows and the C6000 (TI's
// cl6x, measured when the target was written: Target::wcharType is UShort
// there). The widths are pinned with static_assert under each target's macro,
// so that one .expected serves all four; what is printed is the same
// everywhere - the code points a wide literal holds and how the promotions
// read them.
extern "C" int printf(const char *, ...);

#if defined(_WIN32) || defined(__TMS320C6X__)
static_assert(sizeof(wchar_t) == 2, "a 16-bit wchar_t here");
static_assert(sizeof(L"ab") == 6, "three units of two bytes");
static_assert((wchar_t)-1 > 0, "unsigned here");
#else
static_assert(sizeof(wchar_t) == 4, "a 32-bit wchar_t here");
static_assert(sizeof(L"ab") == 12, "three units of four bytes");
static_assert((wchar_t)-1 < 0, "signed here");
#endif

template <class T> int count(const T *s) { int n = 0; while (s[n]) n++; return n; }

int which(int) { return 1; }
int which(unsigned int) { return 2; }

int main() {
    wchar_t c = L'x';
    const wchar_t *s = L"abé€";
    wchar_t arr[] = L"xyz";
    printf("%d %d %d\n", (int)c, count(s), (int)(sizeof(arr) / sizeof(arr[0])));
    printf("%d %d %d %d\n", (int)s[0], (int)s[1], (int)s[2], (int)s[3]);
    // [conv.prom]/2: wchar_t promotes to int wherever int holds every value.
    printf("%d\n", which(c));
    wchar_t w = 0;
    w = (wchar_t)(w - 1);
    // Whether that wraps below zero is the target's sign, pinned above and not printed.
    printf("%d\n", (int)(w == (wchar_t)-1));
    return 0;
}
