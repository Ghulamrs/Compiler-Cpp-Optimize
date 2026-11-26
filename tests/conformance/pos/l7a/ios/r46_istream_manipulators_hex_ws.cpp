#include <iostream>
#include <sstream>
int main() { std::istringstream in("  0x1f 7"); int a, b; in >> std::ws >> std::hex >> a >> std::dec >> b; std::cout << a << ' ' << b << '\n'; return 0; }
