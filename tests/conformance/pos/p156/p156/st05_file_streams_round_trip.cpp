#include <iostream>
#include <fstream>
#include <string>
#include <cstdio>

int main() {
    const char *name = "st05_round_trip.tmp";
    {
        std::ofstream f(name);
        std::cout << "open " << f.is_open() << '\n';
        f << "line one\n" << 99 << ' ' << 2.5 << '\n';
    }
    {
        std::ofstream f(name, std::ios_base::app);
        f << "appended\n";
    }
    {
        std::ifstream f(name);
        std::string a, b;
        int n = 0;
        double d = 0;
        std::getline(f, a);
        f >> n >> d;
        f >> b;
        std::cout << '[' << a << "] " << n << ' ' << d << " [" << b << "] good " << f.good() << '\n';
        f >> b;
        std::cout << "eof " << f.eof() << '\n';
    }
    {
        std::fstream f(name, std::ios_base::in | std::ios_base::out);
        f.seekp(0);
        f << "LINE";
        f.seekg(0);
        std::string a;
        std::getline(f, a);
        std::cout << '[' << a << "]\n";
    }
    std::ifstream missing("st05_no_such_file.tmp");
    std::cout << "missing " << missing.is_open() << ' ' << missing.fail() << '\n';
    std::remove(name);
    return 0;
}
