#include <cstdio>
#include <climits>
#include <string>
static void show(const std::string &s) { std::printf("[%s] ", s.c_str()); }
static void widen(const std::wstring &w) {
    std::string n;
    for (std::size_t i = 0; i < w.size(); i++) n += (char)w[i];
    std::printf("{%s} ", n.c_str());
}
int main() {
    show(std::to_string(1.5));
    show(std::to_string(-0.25f));
    show(std::to_string(1e20));
    show(std::to_string(123.375L));
    show(std::to_string(0.0000001));
    std::printf("\n");
    show(std::to_string(ULLONG_MAX));
    show(std::to_string(ULONG_MAX));
    show(std::to_string(UINT_MAX));
    show(std::to_string(INT_MIN));
    show(std::to_string(LLONG_MIN));
    show(std::to_string(-7L));
    std::printf("\n");
    std::printf("%d\n", (int)std::to_string(1e300).size());
    widen(std::to_wstring(42));
    widen(std::to_wstring(42u));
    widen(std::to_wstring(-42L));
    widen(std::to_wstring(42UL));
    widen(std::to_wstring(-42LL));
    widen(std::to_wstring(42ULL));
    widen(std::to_wstring(2.5f));
    widen(std::to_wstring(2.5));
    widen(std::to_wstring(2.5L));
    std::printf("\n");
    return 0;
}
