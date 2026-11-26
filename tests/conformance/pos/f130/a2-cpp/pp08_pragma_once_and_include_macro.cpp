#include <cstdio>
#define HDR <cstdlib>
#include HDR
#define Q(x) #x
#define INC(x) Q(x)
#include INC(cstring)
int main() { std::printf("%d\n", (int)std::strlen("ab") + std::abs(-1)); return 0; }
