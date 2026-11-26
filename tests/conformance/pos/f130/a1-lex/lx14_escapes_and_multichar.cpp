#include <cstdio>
int main() { std::printf("%d %d %d %d %d %d\n", '\x41', '\101', '\0', (int)sizeof('a'), (int)sizeof(L'a'), (int)'\\'); int m = 'ab'; std::printf("%d\n", m == 0x6162 ? 1 : 0); return 0; }
