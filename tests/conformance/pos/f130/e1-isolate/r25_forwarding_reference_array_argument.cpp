#include <cstdio>
#include <utility>
#include <string>
int f(const std::string &) { return 1; } int f(std::string &&) { return 2; } template <class T> int fs(T &&t) { return f(std::forward<T>(t)); }
int main() { std::printf("%d\n", fs("lit")); return 0; }
