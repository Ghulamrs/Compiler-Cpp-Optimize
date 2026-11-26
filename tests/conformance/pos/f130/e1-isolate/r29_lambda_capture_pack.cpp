#include <cstdio>
template <class... Ts> int cap(Ts... ts) { auto l = [ts...] { return (int)sizeof...(ts); }; return l(); }
int main() { std::printf("%d\n", cap(1, 2.5)); return 0; }
