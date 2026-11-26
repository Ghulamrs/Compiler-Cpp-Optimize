#include <iostream>
#include <string>
#include <cstdio>
struct V { int n; std::string str() const { return std::string(n, 'x'); } };
template <int N> struct W { std::string str() const { return std::string(N, 'w'); } };
inline std::ostream &operator<<(std::ostream &os, const V &v) { return os << v.str(); }
template <int N> inline std::ostream &operator<<(std::ostream &os, const W<N> &w) { return os << w.str(); }
int main() {
    setvbuf(stdout, 0, _IONBF, 0);
    V v = { 3 };
    std::cout << v << " a" << std::endl;
    std::printf("one\n");
    W<2> w;
    std::cout << w << " b" << std::endl;
    std::printf("two\n");
}
