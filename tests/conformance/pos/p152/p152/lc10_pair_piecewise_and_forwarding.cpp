#include <utility>
#include <tuple>
#include <string>
#include <cstdio>
struct Two {
    int a; std::string s;
    Two(int x, const std::string &y) : a(x), s(y) {}
};
int main() {
    std::string k("key");
    std::pair<const std::string, int> p(std::piecewise_construct, std::forward_as_tuple(k), std::tuple<>());
    std::pair<Two, int> q(std::piecewise_construct, std::forward_as_tuple(3, "abc"), std::forward_as_tuple(9));
    std::pair<std::string, int> r("lit", 4);
    std::pair<const std::string, int> s("lit2", 5);
    std::printf("%s %d %d %s %d %s %d %s %d\n", p.first.c_str(), p.second, q.first.a, q.first.s.c_str(), q.second,
                r.first.c_str(), r.second, s.first.c_str(), s.second);
}
