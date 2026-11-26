// Predefined macros of C++11: __cplusplus == 201103L, __STDC_HOSTED__, __func__ ([dcl.fct.def.general]/8), __DATE__ shape, __COUNTER__ is an extension (not used).
#include <cstdio>
#include <cstring>
const char *who() { return __func__; }
struct S { const char *m() { return __func__; } };
int main() {
    std::printf("%ld %d\n", (long)__cplusplus, __cplusplus >= 201103L);
    std::printf("%d\n", __STDC_HOSTED__);
    std::printf("%s %s %s\n", __func__, who(), S().m());
    std::printf("%d %d\n", (int)std::strlen(__DATE__), (int)std::strlen(__TIME__));
#ifdef __cplusplus
    std::printf("cpp\n");
#endif
#if defined(_WIN32) && defined(_WIN64)
    std::printf("win64\n");
#endif
    return 0;
}
