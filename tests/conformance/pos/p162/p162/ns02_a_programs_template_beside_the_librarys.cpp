// [namespace.std] reserves nothing outside std: a program's `vector`, `pair` and `list`, at namespace scope or its own
// namespace's, stand beside the library's, and the library goes on using its own.
#include <cstdio>
#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>
template <class T> struct vector { int mine; T extra; };
template <class T> struct pair { T a, b; };
namespace n {
    template <class T> struct list { T v; int f() const { return 7; } };
    template <class T> struct vector { T v; };
    typedef std::vector<int> Real;
}
int main() {
    vector<int> a;
    a.mine = 1;
    std::vector<int> b(2);
    n::vector<int> c = { 3 };
    pair<int> p = { 1, 2 };
    std::pair<int, char> q(3, 'x');
    n::list<int> l = { 4 };
    std::list<int> m;
    m.push_back(5);
    n::Real r(4);
    std::map<std::string, std::vector<int> > byName;
    byName["k"].push_back(9);
    std::printf("%d %d %d %d %d\n", a.mine, (int)b.size(), c.v, p.b, q.first);
    std::printf("%d %d %d %d %d\n", l.f(), m.front(), (int)r.size(), (int)byName.size(), byName["k"][0]);
    return 0;
}
