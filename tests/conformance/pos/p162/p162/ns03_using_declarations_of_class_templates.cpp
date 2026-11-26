// [namespace.udecl]: a using-declaration names a class template as it names anything else - at namespace scope, in
// a namespace of a program, in a block - and a using-directive reaches one too.
#include <cstdio>
#include <vector>
using std::vector;
namespace k {
    using std::vector;
    vector<int> mine(3);
    template <class T> struct Box { T v; };
}
namespace m { using k::Box; int f() { Box<int> b = { 6 }; return b.v; } }
namespace u { template <class T> struct Only { T v; }; }
int main() {
    vector<int> v(2);
    k::vector<char> w(3);
    using k::Box;
    Box<int> b = { 5 };
    using namespace u;
    Only<int> o = { 7 };
    std::printf("%d %d %d %d %d %d\n", (int)v.size(), (int)w.size(), (int)k::mine.size(), b.v, m::f(), o.v);
    return 0;
}
