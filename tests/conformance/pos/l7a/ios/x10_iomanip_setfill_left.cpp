#include <iostream>
#include <iomanip>
int main() { std::cout << std::setfill('0') << std::setw(4) << 7 << '|' << std::left << std::setw(3) << 1 << '|' << std::endl; return 0; }
