#include <cstdio>
#include <fstream>
#include <iomanip>
#include <string>
#include <iostream>
static void put(const char *name, const char *text) {
    std::ofstream out(name);
    out << text;
}
int main() {
    char name[L_tmpnam];
    if (std::tmpnam(name) == 0) return 1;
    put(name, "0xff 10 777 -12");
    {
        std::ifstream in(name);
        int a = 0, b = 0, c = 0, d = 0;
        in >> std::hex >> a >> b >> std::oct >> c >> std::dec >> d;
        std::cout << a << ' ' << b << ' ' << c << ' ' << d << ' ' << in.fail() << '\n';
    }
    put(name, "999999999999 5");
    {
        std::ifstream in(name);
        int a = 0;
        in >> a;
        std::cout << a << ' ' << in.fail() << '\n';
    }
    put(name, " 5 2.5");
    {
        std::ifstream in(name);
        int a = 9;
        in >> std::noskipws >> a;
        std::cout << a << ' ' << in.fail() << '\n';
    }
    {
        std::ifstream in(name);
        int a = 0;
        double x = 9;
        char sp = 0;
        in >> a >> std::noskipws >> sp >> x;
        std::cout << a << " [" << sp << "] " << x << ' ' << in.fail() << '\n';
    }
    {
        std::ifstream in(name);
        int a = 0;
        double x = 9;
        in >> a >> std::noskipws >> x;
        std::cout << a << ' ' << x << ' ' << in.fail() << '\n';
    }
    std::remove(name);
    std::ofstream shown(name);
    shown << std::showpos << 5 << ' ' << std::left << std::setw(4) << 'x' << '|';
    shown.close();
    std::ifstream back(name);
    std::string line;
    std::getline(back, line);
    std::cout << line << '\n';
    back.close();
    std::remove(name);
    return 0;
}
