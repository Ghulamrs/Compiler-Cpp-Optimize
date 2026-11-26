// [expr.ass]/9 and the library's forwarding members: s += {..}, v = {..}, emplace, emplace_back, make_shared.
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <memory>
struct P { int x; double y; P(int a, double b) : x(a), y(b) {} };
int main() {
    std::string s = "ab";
    s += {'c', 'd'};
    std::vector<int> v;
    v = {1, 2, 3};
    std::vector<P> vp;
    vp.emplace_back(1, 2.5);
    std::map<int, char> m;
    m.emplace(1, 'x');
    std::shared_ptr<P> sp = std::make_shared<P>(3, 4.5);
    std::printf("%s %d %d %d %c %d %g\n", s.c_str(), (int)v.size(), v[2], vp[0].x, m[1], sp->x, sp->y);
    return 0;
}
