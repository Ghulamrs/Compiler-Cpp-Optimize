#include <cstdio>
#include <string>
int f(const std::string &) { return 1; } int f(std::string &&) { return 2; }
int main() { std::printf("%d %d\n", f("lit"), f(std::string("x"))); return 0; }
