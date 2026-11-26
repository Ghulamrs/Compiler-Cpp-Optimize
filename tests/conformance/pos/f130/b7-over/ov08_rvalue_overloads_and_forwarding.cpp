#include <cstdio>
#include <utility>
#include <string>
int f(const std::string &) { return 1; } int f(std::string &&) { return 2; } int g(int &) { return 1; } int g(const int &) { return 2; } int g(int &&) { return 3; }
template <class T> int fw(T &&t) { return g(std::forward<T>(t)); } template <class T> int fs(T &&t) { return f(std::forward<T>(t)); } struct W { std::string s; int get() const & { return 1; } int get() && { return 2; } };
int main() { std::string s = "s"; int i = 1; const int ci = 2; W w; std::printf("%d %d %d %d %d %d %d %d %d %d %d %d\n", f(s), f("lit"), f(std::move(s)), f(std::string("t")), g(i), g(ci), g(3), fw(i), fw(ci), fw(4), fs("lit"), fs(s)); std::printf("%d %d %d\n", w.get(), W().get(), std::move(w).get()); return 0; }
