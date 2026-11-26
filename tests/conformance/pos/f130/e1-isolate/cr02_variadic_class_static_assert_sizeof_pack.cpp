#include <cstdio>
template <class... Ts> struct Holder { static_assert(sizeof...(Ts) > 0, "need one"); int n = 0; };
int main() { Holder<int> h; std::printf("%d\n", h.n); return 0; }
