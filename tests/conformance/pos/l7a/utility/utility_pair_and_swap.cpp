#include <cstdio>
#include <map>
#include <string>
#include <utility>
struct Track {
    int v;
    static int copies, moves;
    Track(int v) : v(v) {}
    Track(const Track &o) : v(o.v) { copies++; }
    Track(Track &&o) noexcept : v(o.v) { moves++; o.v = -1; }
    Track &operator=(const Track &o) { v = o.v; copies++; return *this; }
    Track &operator=(Track &&o) noexcept { v = o.v; moves++; o.v = -1; return *this; }
};
int Track::copies = 0;
int Track::moves = 0;
struct Risky {
    int v;
    Risky(int v) : v(v) {}
    Risky(const Risky &o) : v(o.v) {}
    Risky(Risky &&o) : v(o.v) { o.v = 0; }
};
void counts(const char *what) {
    std::printf("%s: %d copies %d moves\n", what, Track::copies, Track::moves);
    Track::copies = Track::moves = 0;
}
int main() {
    std::pair<int, char> pc(65, 'B');
    std::pair<long, double> pl(pc);
    std::printf("%ld %g\n", pl.first, pl.second);
    std::pair<std::string, int> ps("text", 3);
    std::pair<std::string, long> ps2(std::move(ps));
    std::printf("[%s] %ld [%s]\n", ps2.first.c_str(), ps2.second, ps.first.c_str());
    Track a1(1), a2(2);
    std::pair<Track, Track> pt(a1, a2);
    Track::copies = 0;
    Track a(3), b(4);
    std::pair<Track, Track> pt2(a, b);
    counts("pair(lvalues)");
    std::pair<Track, Track> pt3(std::move(pt2));
    counts("pair(pair&&)");
    pt.swap(pt3);
    std::printf("%d %d %d %d ", pt.first.v, pt.second.v, pt3.first.v, pt3.second.v);
    counts("member swap");
    std::swap(pt, pt3);
    std::printf("%d %d ", pt.first.v, pt3.first.v);
    counts("swap(pair)");
    int x[3] = {1, 2, 3}, y[3] = {7, 8, 9};
    std::swap(x, y);
    std::printf("%d %d %d | %d %d %d\n", x[0], x[1], x[2], y[0], y[1], y[2]);
    Track ta[2] = {Track(5), Track(6)}, tb[2] = {Track(7), Track(8)};
    counts("arrays built");
    std::swap(ta, tb);
    std::printf("%d %d %d %d ", ta[0].v, ta[1].v, tb[0].v, tb[1].v);
    counts("swap(arrays)");
    Track t(9);
    Track t2(std::move_if_noexcept(t));
    counts("move_if_noexcept(noexcept move)");
    Risky r(5);
    Risky r2(std::move_if_noexcept(r));
    std::printf("move_if_noexcept(throwing move): %d %d\n", r.v, r2.v);
    std::printf("%d %d\n", (int)std::tuple_size<std::pair<int, char> >::value, (int)sizeof(std::tuple_element<1, std::pair<int, double> >::type));
    std::tuple_element<0, std::pair<char, int> >::type c = 'z';
    std::printf("%c\n", c);
    std::piecewise_construct_t pw = std::piecewise_construct;
    (void)pw;
    std::map<std::string, int> m;
    m.insert(std::make_pair(std::string("k"), 1));
    std::printf("%d %d\n", (int)m.size(), m["k"]);
    return 0;
}
