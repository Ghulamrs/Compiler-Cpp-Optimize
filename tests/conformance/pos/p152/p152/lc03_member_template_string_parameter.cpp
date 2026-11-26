#include <string>
#include <cstdio>
template <class C> int f(const std::basic_string<C> &s) { return (int)s.size(); }
struct B {
    int n;
    template <class C> explicit B(const std::basic_string<C> &s, std::size_t pos = 0, C zero = C('0')) : n((int)s.size() + (int)pos + (zero == '0')) {}
};
int main() {
    std::string s("abc");
    B b(s);
    std::printf("%d %d\n", f(s), b.n);
}
