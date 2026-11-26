// [over.match.oper]/3: operator templates of a nested namespace join only where a using-directive or ADL reaches them.
#include <cstdio>
namespace N { struct S { int v; }; bool operator==(const S &a, const S &b) { return a.v == b.v; }
namespace R { template <class T> bool operator!=(const T &a, const T &b) { return !(a == b); } } }
namespace N { template <class T> bool same(const T &a, const T &b) { return a == b; } }
int main() {
    N::S a = {1}, b = {2};
    { using namespace N::R; std::printf("%d\n", (int)(a != b)); }
    std::printf("%d %d\n", (int)(a == b), (int)same(a, a));
    return 0;
}
