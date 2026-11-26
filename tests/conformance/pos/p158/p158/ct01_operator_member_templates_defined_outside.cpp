#include <cstdio>
#include <string>

// [temp.mem]/1: a member template of an ordinary class, an operator function among them, defined outside it.
struct Less {
    template <class T> bool operator()(const T &a, const T &b) const;
    template <class T> bool operator<(const T &) const;
    template <class T> Less &operator<<(const T &v);
    int seen;
    Less() : seen(0) {}
};
template <class T> bool Less::operator()(const T &a, const T &b) const { return a < b; }
template <class T> bool Less::operator<(const T &) const { return true; }
template <class T> Less &Less::operator<<(const T &v) {
    seen += static_cast<int>(sizeof v);
    return *this;
}

int main() {
    Less l;
    l << 'a' << 1.0;
    std::printf("%d %d %d %d\n", (int)l(1, 2), (int)l(std::string("b"), std::string("a")), (int)(l < 3), l.seen);
    return 0;
}
