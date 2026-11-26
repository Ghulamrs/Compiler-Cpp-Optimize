#include <cerrno>
#include <cfenv>
#include <cinttypes>
#include <ciso646>
#include <clocale>
#include <csetjmp>
#include <csignal>
#include <cstdalign>
#include <cstdbool>
#include <ccomplex>
#include <ctgmath>
#include <cuchar>
#include <cwchar>
#include <cwctype>
#include <cstdio>
#include <cstring>

static std::jmp_buf env;
static volatile std::sig_atomic_t got = 0;
static void handler(int s) { got = s; }

int main() {
    std::int64_t big = -1234567890123LL;
    std::printf("%" PRId64 " %" PRIx32 " %" PRIuMAX "\n", big, (std::uint32_t)255, (std::uintmax_t)42);
    std::intmax_t parsed = std::strtoimax("-77", 0, 10);
    std::imaxdiv_t q = std::imaxdiv(17, 5);
    std::printf("%lld %lld %lld %lld\n", (long long)parsed, (long long)q.quot, (long long)q.rem, (long long)std::imaxabs(-9));

    std::feclearexcept(FE_ALL_EXCEPT);
    volatile double zero = 0.0, one = 1.0;
    volatile double r = one / zero;
    (void)r;
    std::printf("divbyzero %d\n", std::fetestexcept(FE_DIVBYZERO) != 0);
    int was = std::fegetround();
    std::printf("round %d\n", std::fesetround(FE_UPWARD) == 0);
    std::fesetround(was);
    std::feraiseexcept(FE_INEXACT);
    std::printf("inexact %d\n", std::fetestexcept(FE_INEXACT) != 0);

    std::printf("locale %d %s\n", std::setlocale(LC_ALL, "C") != 0, std::localeconv()->decimal_point);

    int jumped = setjmp(env);
    if (jumped == 0) std::longjmp(env, 3);
    std::printf("longjmp %d\n", jumped);

    std::signal(SIGINT, handler);
    std::raise(SIGINT);
    std::printf("signal %d\n", (int)got);

    std::printf("macros %d %d\n", __alignas_is_defined, __bool_true_false_are_defined);

    const wchar_t *w = L"Wide Text";
    wchar_t buf[32];
    std::wcscpy(buf, w);
    std::wcscat(buf, L"!");
    std::printf("wcs %d %d %d\n", (int)std::wcslen(buf), (int)(std::wcschr(buf, L'T') - buf), std::wcscmp(buf, L"Wide Text!"));
    wchar_t num[16];
    std::swprintf(num, 16, L"%d-%ls", 42, L"x");
    std::printf("swprintf %d %ld\n", (int)std::wcslen(num), std::wcstol(num, 0, 10));
    wchar_t m[4];
    std::wmemset(m, L'z', 3);
    m[3] = 0;
    std::printf("wmem %d %d\n", std::wmemcmp(m, L"zzz", 3), (int)(std::wmemchr(m, L'z', 3) == m));
    std::mbstate_t st;
    std::memset(&st, 0, sizeof st);
    wchar_t out = 0;
    std::size_t n = std::mbrtowc(&out, "A", 1, &st);
    std::printf("mbrtowc %d %d %d\n", (int)n, (int)out, std::mbsinit(&st));
    std::printf("wctype %d %d %d\n", std::iswalpha(L'a') != 0, std::iswdigit(L'5') != 0, (int)std::towupper(L'q'));
    char16_t c16 = 0;
    std::memset(&st, 0, sizeof st);
    std::size_t k = std::mbrtoc16(&c16, "B", 1, &st);
    std::printf("mbrtoc16 %d %d\n", (int)k, (int)c16);
    std::complex<double> z(1, 1);
    std::printf("tgmath %g %g\n", std::abs(z), (double)std::sqrt(4.0f));
    std::printf("errno %d\n", ERANGE);
    return 0;
}
