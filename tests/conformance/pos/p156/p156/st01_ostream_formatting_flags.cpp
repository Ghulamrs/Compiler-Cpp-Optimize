#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>

static void show(const char *what, const std::ostringstream &o) { std::cout << what << " [" << o.str() << "]\n"; }

int main() {
    { std::ostringstream o; o << std::hex << 255 << ' ' << std::uppercase << 255 << ' ' << std::showbase << 255; show("hex", o); }
    { std::ostringstream o; o << std::oct << 8 << ' ' << std::dec << -7; show("oct", o); }
    { std::ostringstream o; o << std::setw(6) << std::setfill('*') << 42; show("setw", o); }
    { std::ostringstream o; o << std::left << std::setw(5) << "ab" << '|'; show("left", o); }
    { std::ostringstream o; o << std::internal << std::setw(6) << -42; show("internal", o); }
    { std::ostringstream o; o << std::fixed << std::setprecision(3) << 3.14159 << ' ' << 2.5; show("fixed", o); }
    { std::ostringstream o; o << std::scientific << std::setprecision(2) << 12345.678; show("scientific", o); }
    { std::ostringstream o; o << 0.1 << ' ' << 1e20 << ' ' << 100.0; show("general", o); }
    { std::ostringstream o; o << std::showpos << 5 << ' ' << 0 << std::noshowpos << ' ' << 5; show("showpos", o); }
    { std::ostringstream o; o << true << ' ' << std::boolalpha << false << ' ' << true; show("bool", o); }
    { std::ostringstream o; o << 'x' << "yz" << std::string("w") << static_cast<short>(-3) << 7u << 8L << 9ULL; show("mixed", o); }
    { std::ostringstream o; o.width(4); o << 1 << 2; show("width once", o); }
    { std::ostringstream o; o << std::setprecision(4) << 1.0 / 3; show("precision", o); }
    return 0;
}
