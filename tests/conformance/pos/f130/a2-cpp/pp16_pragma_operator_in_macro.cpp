#include <cstdio>
#define DO_PRAGMA(x) _Pragma(#x)
DO_PRAGMA(pack(push, 1))
struct S { char c; int i; };
#pragma pack(pop)
struct T { char c; int i; };
int main() { std::printf("%d %d\n", (int)sizeof(S), (int)sizeof(T)); return 0; }
