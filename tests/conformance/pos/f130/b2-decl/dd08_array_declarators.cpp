#include <cstdio>
extern int inc[]; int inc[] = {1, 2, 3}; int tbl[][2] = {{1, 2}, {3, 4}, {5, 6}}; const char *names[] = {"a", "b"}; char str[] = "xyz"; wchar_t ws[] = L"ab"; char16_t u16[] = u"abc"; char32_t u32[] = U"ab"; int zero[3] = {}; int (*pz)[3] = &zero;
int main() { std::printf("%d %d %d %d %d %d %d %d\n", (int)(sizeof(inc) / sizeof *inc), (int)(sizeof(tbl) / sizeof tbl[0]), (int)sizeof(names) / (int)sizeof(void*), (int)sizeof(str), (int)(sizeof(ws) / sizeof(wchar_t)), (int)(sizeof(u16) / 2), (int)(sizeof(u32) / 4), (*pz)[2]); return 0; }
