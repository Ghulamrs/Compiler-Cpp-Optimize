// std::forward through two forwarding layers for a prvalue and an xvalue of a const object.
#include <cstdio>
#include <utility>
const char *sink(int &) { return "lv"; }
const char *sink(const int &) { return "clv"; }
const char *sink(int &&) { return "rv"; }
const char *sink(const int &&) { return "crv"; }
template <class T> const char *fwd(T &&t) { return sink(std::forward<T>(t)); }
template <class T> const char *fwd2(T &&t) { return fwd(std::forward<T>(t)); }
int main() { int x = 1; const int cx = 2; std::printf("%s %s %s %s\n", fwd2(x), fwd2(4), fwd2(std::move(cx)), fwd2(std::move(x))); return 0; }
