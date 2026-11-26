#include <cstdio>
#include <cstdlib>
#include <cstring>
static void first() { std::printf("first registered, last run\n"); }
static void second() { std::printf("second registered, first run\n"); }
int main() {
    std::atexit(first);
    std::atexit(second);
    std::printf("%lld %lld %d\n", std::atoll("-9000000000123"), std::atoll("  42xyz"), (int)sizeof(std::atoll("1")));
    char mb[8] = "";
    wchar_t w = 0;
    wchar_t ws[8];
    std::size_t n = std::mbstowcs(ws, "hey", 8);
    int k = std::mbtowc(&w, "Z", 1);
    int back = std::wctomb(mb, L'q');
    std::size_t m = std::wcstombs(mb, L"ok", sizeof mb);
    std::printf("%d %d %d %d %d %d %s %d\n", std::mblen("abc", 3), std::mblen("", 1), (int)n, (int)ws[1], k, (int)w, mb, back + (int)m);
    std::printf("%d %d\n", (int)sizeof(std::div_t), (int)sizeof(std::lldiv_t));
    return 0;
}
