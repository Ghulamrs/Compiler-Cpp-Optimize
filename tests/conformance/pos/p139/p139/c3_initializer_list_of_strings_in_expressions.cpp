// R21: an initializer_list<std::string> made in an argument from a mix of strings and literals.
#include <cstdio>
#include <initializer_list>
#include <string>
std::size_t total(std::initializer_list<std::string> l) { std::size_t n = 0; for (const std::string &s : l) n += s.size(); return n; }
int main() {
    std::string x = "four";
    std::printf("%d %d\n", (int)total({std::string("ab"), "cde", x}), (int)total({}));
    return 0;
}