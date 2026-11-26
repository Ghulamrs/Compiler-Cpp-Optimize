#include <iostream>
#include <iomanip>
#include <sstream>
int main() {
    std::cout << (std::cin.tie() == &std::cout) << (std::cerr.tie() == &std::cout) << (std::clog.tie() == 0) << (std::cout.tie() == 0) << '\n';
    std::ostringstream other;
    std::ostream *was = std::cin.tie(&other);
    std::cout << (was == &std::cout) << (std::cin.tie() == &other) << '\n';
    std::cin.tie(was);
    std::ostringstream fmt;
    fmt << std::hex << std::showbase << std::left << std::setfill('*') << std::setprecision(3);
    fmt.width(9);
    std::ostringstream copy;
    copy.copyfmt(fmt);
    copy << 255;
    copy << '|';
    copy << 3.14159;
    std::cout << copy.str() << ' ' << copy.fill() << ' ' << (long)copy.precision() << '\n';
    std::cout << copy.narrow('a', '?') << copy.widen('b') << '\n';
    bool first = std::ios_base::sync_with_stdio(true);
    std::cout << first << '\n';
    int i0 = std::ios_base::xalloc();
    int i1 = std::ios_base::xalloc();
    std::cout << i0 << ' ' << i1 << '\n';
    std::ios_base::Init init;
    (void)init;
    std::streampos pos = 5;
    std::streamoff off = std::streamoff(pos) + 2;
    std::cout << sizeof(std::streamoff) << ' ' << off << '\n';
    std::ostringstream flagged;
    flagged.setstate(std::ios::failbit);
    flagged << "not written";
    flagged.put('x');
    flagged.write("y", 1);
    std::cout << '[' << flagged.str() << "] " << flagged.fail() << '\n';
    return 0;
}
