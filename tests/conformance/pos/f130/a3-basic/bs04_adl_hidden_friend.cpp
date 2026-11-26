#include <cstdio>
namespace N { struct S { friend int twice(S) { return 2; } }; }
int main() { N::S s; std::printf("%d\n", twice(s)); return 0; }
