#include <cstdio>
struct S { int m; }; template <class C, class M> int mem(M C::*) { return (int)sizeof(M); }
int main() { std::printf("%d\n", mem(&S::m)); return 0; }
