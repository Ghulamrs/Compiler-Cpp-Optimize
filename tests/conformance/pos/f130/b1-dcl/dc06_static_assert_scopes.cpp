#include <cstdio>
static_assert(sizeof(int) == 4, "int"); struct S { static_assert(sizeof(char) == 1, "char"); int v; }; template <class T> struct W { static_assert(sizeof(T) >= 1, "T"); T t; };
int main() { static_assert(true, "fn"); W<int> w{1}; S s{2}; std::printf("%d %d\n", w.t, s.v); return 0; }
