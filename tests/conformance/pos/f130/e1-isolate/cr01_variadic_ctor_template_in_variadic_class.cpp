#include <cstdio>
template <class... Ts> struct Holder { int n; template <class... Us> Holder(Us &&... us) : n(sizeof...(Us)) {} };
int main() { Holder<int> h(1, 2, 3); std::printf("%d\n", h.n); return 0; }
