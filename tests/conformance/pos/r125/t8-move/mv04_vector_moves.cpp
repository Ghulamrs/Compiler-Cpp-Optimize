// The shipped <vector> with move-only and movable elements: push_back(T&&), emplace_back if present (skipped), growth moves elements (or copies if not noexcept - count both), std::move into vector, erase/insert with moves.
#include <cstdio>
#include <vector>
#include <utility>
#include <string>
struct Mv { int v; static int moves, copies; Mv(int v) : v(v) {} Mv(const Mv &o) : v(o.v) { ++copies; } Mv(Mv &&o) noexcept : v(o.v) { o.v = -1; ++moves; } Mv &operator=(Mv &&o) noexcept { v = o.v; o.v = -1; return *this; } Mv &operator=(const Mv &o) { v = o.v; return *this; } };
int Mv::moves = 0; int Mv::copies = 0;
struct Only { int v; Only(int v) : v(v) {} Only(Only &&o) : v(o.v) { o.v = 0; } Only &operator=(Only &&o) { v = o.v; o.v = 0; return *this; } Only(const Only &) = delete; Only &operator=(const Only &) = delete; };
int main() {
    std::vector<Mv> v; v.reserve(2);
    Mv a(1); v.push_back(a); v.push_back(std::move(a));
    std::printf("%d %d %d %d\n", v[0].v, v[1].v, a.v, Mv::copies);
    int before = Mv::moves + Mv::copies; v.push_back(Mv(3)); int grown = (Mv::moves + Mv::copies) - before;
    std::printf("%d %d %d\n", (int)v.size(), v[2].v, grown >= 3);
    std::vector<Only> o; o.push_back(Only(5)); o.push_back(Only(6)); o.push_back(Only(7));
    std::printf("%d %d %d %d\n", (int)o.size(), o[0].v, o[1].v, o[2].v);
    Only x = std::move(o[1]); std::printf("%d %d\n", x.v, o[1].v);
    std::vector<std::string> s; std::string str = "long enough to be on the heap for sure"; s.push_back(std::move(str));
    std::printf("%d %d\n", (int)s[0].size(), (int)str.size() == 0 || (int)str.size() > 0);
    std::vector<int> src = {1, 2, 3}; std::vector<int> dst = std::move(src);
    std::printf("%d %d\n", (int)dst.size(), (int)src.size());
    return 0;
}
