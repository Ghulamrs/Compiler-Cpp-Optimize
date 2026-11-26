#include <cstdio>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
int main() {
    std::istringstream a("0123456789");
    a.seekg(4);
    std::cout << (char)a.get() << ' ';
    std::cout << a.tellg() << ' ';
    a.seekg(-2, std::ios::end);
    std::cout << (char)a.get() << ' ';
    a.seekg(-3, std::ios::cur);
    std::cout << (char)a.get() << ' ';
    a.seekg(20);
    std::cout << a.rdstate() << '\n';
    std::istringstream b("1,2");
    int x = 0, y = 0;
    char comma = 0;
    b >> x >> comma >> y;
    std::cout << b.rdstate() << ' ';
    std::streampos at = b.tellg();
    std::cout << at << ' ' << b.rdstate() << ' ';
    b.seekg(0, std::ios::beg);
    std::cout << b.rdstate() << ' ';
    b.clear();
    b.seekg(2);
    b >> y;
    std::cout << y << '\n';
    std::ostringstream o;
    o << "hello";
    std::cout << o.tellp() << ' ';
    o.seekp(0);
    o << "J";
    std::cout << o.str() << ' ' << o.tellp() << ' ';
    o.seekp(0, std::ios::end);
    o << '!';
    o.seekp(-1, std::ios::cur);
    o << '?';
    std::cout << o.str() << ' ';
    o.seekp(99);
    std::cout << o.rdstate() << '\n';
    std::ostringstream p;
    p.str("abc");
    p << "Z";
    std::ostringstream q("abc", std::ios::ate);
    q << "Z";
    std::ostringstream r(std::string("xyz"));
    r << "W";
    std::cout << p.str() << ' ' << q.str() << ' ' << r.str() << '\n';
    char name[L_tmpnam];
    if (std::tmpnam(name) == 0) return 1;
    std::ofstream out(name);
    out << "abcdefgh";
    std::cout << out.tellp() << ' ';
    out.seekp(2);
    out << "XY";
    out.close();
    std::ifstream in(std::string(name), std::ios::in);
    std::string all;
    in >> all;
    std::cout << all << ' ' << in.rdstate() << ' ';
    in.clear();
    in.seekg(3, std::ios::beg);
    std::cout << in.tellg() << ' ';
    std::cout << (char)in.get() << ' ';
    in.seekg(-1, std::ios::end);
    std::cout << (char)in.get() << '\n';
    in.close();
    std::ifstream again;
    again.open(std::string(name), std::ios::in);
    char first = 0;
    again >> first;
    std::cout << again.is_open() << ' ' << first << '\n';
    again.close();
    std::remove(name);
    return 0;
}
