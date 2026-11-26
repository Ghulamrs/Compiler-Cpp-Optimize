#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
int main() {
    std::ostringstream o;
    o << std::boolalpha << true << ' ' << false << ' ' << std::setw(7) << true << '|' << std::left << std::setw(7) << false << '|' << std::right << std::noboolalpha << true << '\n';
    o << std::hex << std::showbase << 255 << ' ' << std::uppercase << 255 << ' ' << std::nouppercase << std::oct << 8 << ' ' << 0 << ' ' << std::hex << 0 << std::noshowbase << std::dec << '\n';
    o << std::internal << std::setfill('*') << std::setw(8) << -42 << '|' << std::setw(8) << std::showbase << std::hex << 42 << std::dec << std::noshowbase << '|' << std::setw(6) << std::showpos << 7 << std::noshowpos << '|' << std::setfill(' ') << std::right << '\n';
    o << std::left << std::setw(5) << 'c' << '|' << std::setw(6) << "ab" << '|' << std::setw(6) << std::string("str") << '|' << std::right << std::setw(4) << 'd' << '|' << std::internal << std::setw(4) << "in" << '|' << '\n';
    o << std::showpos << 3 << ' ' << 0 << ' ' << -3 << ' ' << 2.5 << ' ' << 7u << std::noshowpos << '\n';
    o << std::showpoint << 2.0 << ' ' << std::setprecision(3) << 1.0 << std::noshowpoint << ' ' << 1.0 << std::setprecision(6) << '\n';
    o << std::scientific << std::uppercase << 12345.678 << ' ' << std::nouppercase << 12345.678 << ' ' << std::fixed << std::uppercase << 1.5 << std::nouppercase << ' ' << std::defaultfloat << 0.0001 << ' ' << std::uppercase << 1e-10 << std::nouppercase << '\n';
    o << std::hexfloat << 1.5 << ' ' << -0.25 << ' ' << std::uppercase << 10.0 << std::nouppercase << std::defaultfloat << '\n';
    o << std::setiosflags(std::ios::hex | std::ios::showbase) << 16 << ' ' << std::resetiosflags(std::ios::showbase) << 16 << ' ' << std::setbase(8) << 16 << ' ' << std::setbase(10) << 16 << ' ' << std::setbase(0) << 16 << ' ' << std::setbase(16) << -1 << std::dec << '\n';
    std::ios_base::fmtflags saved = o.flags();
    o << std::hex << std::uppercase << std::left;
    o.flags(saved);
    o << 255 << ' ' << ((o.flags() & std::ios::basefield) == std::ios::dec) << ' ' << ((o.flags() & std::ios::skipws) != 0) << ' ' << (long)o.precision() << '\n';
    o.setf(std::ios::hex);
    o << 255 << ' ';
    o.unsetf(std::ios::dec);
    o << 255 << '\n';
    std::cout << o.str();
    std::cout << ((std::cerr.flags() & std::ios::unitbuf) != 0) << ' ' << ((std::cout.flags() & std::ios::unitbuf) != 0) << ' ' << ((std::clog.flags() & std::ios::unitbuf) != 0) << '\n';
    std::cout << std::unitbuf << "unit" << std::nounitbuf << ' ' << ((std::cout.flags() & std::ios::unitbuf) != 0) << std::endl;
    return 0;
}
