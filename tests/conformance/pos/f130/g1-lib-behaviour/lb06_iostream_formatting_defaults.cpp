#include <iostream>
#include <iomanip>
int main() { std::cout << 1.0 << ' ' << 0.1 << ' ' << 1e10 << ' ' << 123456789.0 << ' ' << 1.5f << ' ' << -0.0 << ' ' << 100000000000000000000.0 << '\n'; std::cout << std::setprecision(3) << 3.14159 << ' ' << std::fixed << 2.0 << ' ' << std::setw(6) << 1.5 << '|' << std::setw(2) << "abc" << '|' << (char)65 << (signed char)66 << (unsigned char)67 << ' ' << (short)-1 << ' ' << 077 << '\n'; return 0; }
