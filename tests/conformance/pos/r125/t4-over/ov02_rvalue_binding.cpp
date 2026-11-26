// [over.ics.rank]/3: rvalue reference binding preferred for rvalues; const& for lvalues; const T&& vs T&; std::move argument; returned temporaries; literals.
#include <cstdio>
#include <utility>
#include <string>
const char *f(int &) { return "int&"; }
const char *f(const int &) { return "const int&"; }
const char *f(int &&) { return "int&&"; }
const char *g(const std::string &) { return "const&"; }
const char *g(std::string &&) { return "&&"; }
const char *h(const int &) { return "const&"; }
const char *h(int &&) { return "&&"; }
template <class T> const char *fw(T &&t) { return f(std::forward<T>(t)); }
std::string make() { return "m"; }
int main() {
    int x = 1; const int cx = 2; std::string s = "s";
    std::printf("%s %s %s %s %s\n", f(x), f(cx), f(3), f(std::move(x)), f(x + 1));
    std::printf("%s %s %s %s\n", g(s), g(make()), g(std::move(s)), g("lit"));
    std::printf("%s %s %s\n", h(x), h(cx), h(4));
    std::printf("%s %s %s %s\n", fw(x), fw(cx), fw(5), fw(std::move(x)));
    const int &&crr = 6; std::printf("%s\n", f(crr));
    return 0;
}
