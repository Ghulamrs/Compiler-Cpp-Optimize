#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
static void show(const char *what, const std::ios &s) {
    std::cout << what << ": good " << s.good() << " eof " << s.eof() << " fail " << s.fail() << " bad " << s.bad() << " bool " << (bool)s << " not " << !s << '\n';
}
int main() {
    std::cout << std::ios::goodbit << ' ' << std::ios::eofbit << ' ' << std::ios::failbit << ' ' << std::ios::badbit << '\n';
    std::cout << std::ios::beg << ' ' << std::ios::cur << ' ' << std::ios::end << '\n';
    std::istringstream a("12");
    int x = 0;
    a >> x;
    std::cout << x << ' ';
    show("last field", a);
    int y = 7;
    a >> y;
    std::cout << y << ' ';
    show("after the end", a);
    std::istringstream b("   ");
    int z = 5;
    b >> z;
    std::cout << z << ' ';
    show("only spaces", b);
    std::istringstream c("abc");
    c.setstate(std::ios::badbit);
    show("badbit", c);
    c.clear();
    show("cleared", c);
    c.clear(std::ios::eofbit);
    std::cout << c.rdstate() << ' ';
    show("eofbit alone", c);
    std::istringstream d("q");
    d.exceptions(std::ios::failbit | std::ios::badbit);
    std::cout << d.exceptions() << '\n';
    try {
        int k = 0;
        d >> k;
        std::cout << "no throw\n";
    } catch (std::ios_base::failure &e) {
        std::cout << "failure: " << e.what() << '\n';
    }
    std::istringstream e("");
    char ch = 'k';
    e >> ch;
    try {
        e.exceptions(std::ios::eofbit);
        std::cout << "no throw\n";
    } catch (std::exception &f) {
        std::cout << "on setting the mask: " << f.what() << '\n';
    }
    std::istringstream g("x");
    char gc = 0;
    g >> gc;
    g >> gc;
    std::cout << g.rdstate() << ' ';
    g.str("5");
    std::cout << g.rdstate() << ' ';
    g.clear();
    int gv = 0;
    g >> gv;
    std::cout << gv << '\n';
    std::ofstream o;
    show("ofstream with no file", o);
    o << 1;
    show("written to", o);
    std::ifstream i;
    show("ifstream with no file", i);
    int iv = 3;
    i >> iv;
    std::cout << iv << ' ';
    show("read from", i);
    std::ifstream missing("no such file here.txt");
    std::cout << missing.is_open() << ' ';
    show("missing file", missing);
    return 0;
}
