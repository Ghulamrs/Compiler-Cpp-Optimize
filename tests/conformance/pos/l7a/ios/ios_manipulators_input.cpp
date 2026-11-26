#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
int main() {
    std::istringstream a("ff 0x1F 17 077 9");
    int h1 = 0, h2 = 0, o1 = 0, o2 = 0, d1 = 0;
    a >> std::hex >> h1 >> h2 >> std::oct >> o1 >> o2 >> std::dec >> d1;
    std::cout << h1 << ' ' << h2 << ' ' << o1 << ' ' << o2 << ' ' << d1 << ' ' << a.fail() << '\n';
    std::istringstream b("0x1f 017 23 -0x10");
    int i1 = 0, i2 = 0, i3 = 0, i4 = 0;
    b >> std::setbase(0) >> i1 >> i2 >> i3 >> i4;
    std::cout << i1 << ' ' << i2 << ' ' << i3 << ' ' << i4 << ' ' << b.fail() << '\n';
    std::istringstream c("  x y");
    char c1 = 0, c2 = 0, c3 = 0;
    c >> std::noskipws >> c1 >> c2 >> c3 >> std::skipws;
    std::cout << '[' << c1 << c2 << c3 << "] " << c.fail() << '\n';
    std::istringstream d(" word");
    std::string w = "unchanged";
    d >> std::noskipws >> w;
    std::cout << '[' << w << "] " << d.fail() << '\n';
    std::istringstream e(" 5");
    int n = 9;
    e >> std::noskipws >> n;
    std::cout << n << ' ' << e.fail() << '\n';
    std::istringstream f("   tail");
    std::string t;
    f >> std::noskipws >> std::ws >> t;
    std::cout << '[' << t << "] " << f.fail() << '\n';
    std::istringstream g("1a 1A");
    unsigned u1 = 0, u2 = 0;
    g >> std::setiosflags(std::ios::hex) >> u1 >> u2;
    std::cout << u1 << ' ' << u2 << ' ' << ((g.flags() & std::ios::hex) != 0) << '\n';
    std::istringstream k("12 34");
    int k1 = 0, k2 = 0;
    k >> std::resetiosflags(std::ios::skipws) >> k1 >> k2;
    std::cout << k1 << ' ' << k2 << ' ' << k.fail() << '\n';
    return 0;
}
