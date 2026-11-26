#include <cstdio>
#include <map>
#include <string>
#include <utility>
struct Track {
    int v;
    static int copies, moves;
    Track(int v) : v(v) {}
    Track(const Track &o) : v(o.v) { copies++; }
    Track &operator=(const Track &o) { v = o.v; copies++; return *this; }
    Track &operator=(Track &&o) { v = o.v; moves++; o.v = -1; return *this; }
};
int Track::copies = 0;
int Track::moves = 0;
struct Wide {
    long v;
    Wide(long v) : v(v) {}
    Wide &operator=(const Track &t) { v = t.v * 10; return *this; }
    Wide &operator=(Track &&t) { v = t.v * 100; t.v = -1; return *this; }
};
int main() {
    std::pair<long, double> a(1, 2.5);
    std::pair<int, float> b(3, 4.5f);
    a = b;
    std::printf("%ld %g\n", a.first, a.second);
    std::pair<std::string, int> s("x", 1), t("y", 2);
    std::pair<std::string, long> u("z", 9);
    u = t;
    std::printf("%s %ld [%s]\n", u.first.c_str(), u.second, t.first.c_str());
    u = std::move(s);
    std::printf("%s %ld [%s]\n", u.first.c_str(), u.second, s.first.c_str());
    std::map<int, std::string> m;
    m[7] = "seven";
    std::pair<int, std::string> copy(0, "");
    copy = *m.begin();
    std::printf("%d %s\n", copy.first, copy.second.c_str());
    std::pair<Wide, int> w(Wide(0), 0);
    std::pair<Track, int> tr(Track(4), 1);
    w = tr;
    std::printf("%ld %d ", w.first.v, tr.first.v);
    w = std::move(tr);
    std::printf("%ld %d\n", w.first.v, tr.first.v);
    std::pair<Track, int> t2(Track(5), 2), t3(Track(6), 3);
    t2 = t3;
    t2 = std::move(t3);
    std::printf("%d %d %d\n", Track::copies, Track::moves, t3.first.v);
    return 0;
}
