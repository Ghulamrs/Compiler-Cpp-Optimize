#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
int main() {
    std::istringstream a("2.5 -1e3 7");
    long double ld = 0;
    a >> ld;
    std::cout << (double)ld << ' ';
    a >> ld;
    std::cout << (double)ld << ' ' << a.rdstate() << '\n';
    int local = 0;
    void *p = &local;
    std::ostringstream o;
    o << p;
    std::istringstream pin(o.str());
    void *q = 0;
    pin >> q;
    std::cout << (q == p) << ' ' << pin.fail() << '\n';
    std::istringstream w("abcdefgh next");
    char word[16];
    w >> std::setw(4) >> word;
    std::cout << word << ' ';
    w >> word;
    std::cout << word << ' ';
    w >> word;
    std::cout << word << ' ' << w.rdstate() << '\n';
    std::istringstream sw("abcdefgh");
    std::string part;
    sw >> std::setw(3) >> part;
    std::cout << part << ' ' << sw.width() << '\n';
    std::istringstream cc(" x y");
    signed char sc = 0;
    unsigned char uc = 0;
    cc >> sc >> uc;
    std::cout << (char)sc << (char)uc << '\n';
    std::istringstream sh("40000 -40000 123 70000");
    short s1 = 0, s2 = 0;
    sh >> s1;
    std::cout << s1 << ' ' << sh.fail() << ' ';
    sh.clear();
    sh >> s2;
    std::cout << s2 << ' ' << sh.fail() << ' ';
    sh.clear();
    unsigned short us = 0;
    sh >> us;
    std::cout << us << ' ';
    sh >> us;
    std::cout << us << ' ' << sh.fail() << '\n';
    std::istringstream bn("0 1 2");
    bool b0 = true, b1 = false, b2 = false;
    bn >> b0 >> b1;
    std::cout << b0 << b1 << ' ';
    bn >> b2;
    std::cout << b2 << ' ' << bn.fail() << '\n';
    std::istringstream ba("true false maybe");
    bool t = false, f = true, m = true;
    ba >> std::boolalpha >> t >> f;
    std::cout << t << f << ' ';
    ba >> m;
    std::cout << m << ' ' << ba.fail() << '\n';
    std::istringstream bx("x");
    bool bv = true;
    bx >> bv;
    std::cout << bv << ' ' << bx.fail() << '\n';
    return 0;
}
