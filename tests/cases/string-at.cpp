// A22's last name: string::at, checked, throwing out_of_range - <string>
// alone brings it, and <stdexcept> first does too.
#include <string>
#include <cstdio>
int main() {
    std::string s("abc");
    const std::string &c = s;
    s.at(1) = 'X';
    int caught = 0;
    try { s.at(3) = 'q'; } catch (std::out_of_range &e) { caught = 1; std::printf("%s ", e.what()); }
    std::printf("%s %c %d\n", s.c_str(), c.at(0), caught);
    return 0;
}
