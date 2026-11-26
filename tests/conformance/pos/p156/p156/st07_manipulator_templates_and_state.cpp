#include <iostream>
#include <sstream>
#include <string>

typedef std::ostream &(*Manip)(std::ostream &);

int main() {
    std::ostringstream o;
    o << "a" << std::endl << "b" << std::ends << "c" << std::flush;
    const std::string s = o.str();
    std::cout << s.size() << ' ' << (s[1] == '\n') << ' ' << (s[3] == '\0') << '\n';

    Manip m = std::endl;
    std::ostringstream p;
    p << 1;
    m(p);
    std::cout << p.str().size() << '\n';

    std::istringstream in("   x");
    in >> std::ws;
    std::cout << static_cast<char>(in.peek()) << '\n';

    std::ostringstream e;
    e.exceptions(std::ios_base::badbit | std::ios_base::failbit);
    try {
        e.setstate(std::ios_base::failbit);
        std::cout << "no throw\n";
    } catch (const std::ios_base::failure &) {
        std::cout << "failure thrown\n";
    }

    std::ostringstream c;
    c << std::hex << std::showbase;
    std::ostringstream d;
    d.copyfmt(c);
    d << 255;
    std::cout << d.str() << ' ' << !d << ' ' << static_cast<bool>(d) << '\n';
    std::cout.flush();
    std::cerr << "";
    std::clog << "";
    return 0;
}
