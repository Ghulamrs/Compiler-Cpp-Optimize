#include <cstdio>
template <class... Ts> struct Holder { int n; template <class... Us> Holder(Us &&... us) : n(sizeof...(Us)) {} };
int main() { Holder<int, char> k; std::printf("%d\n", k.n); return 0; }
