#include <iosfwd>
#include <iostream>
#include <ostream>
std::ostream &tag(std::ostream &o) { return o << "[tag]"; } void print(std::ostream &o, std::ostream &(*m)(std::ostream &)) { o << m; }
int main() { print(std::cout, tag); print(std::cout, std::endl); std::cout << tag << std::endl; std::ostream &r = std::cout << 1; r << '\n'; return 0; }
