#include <cstdio>
#include <bitset>
#include <string>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <functional>
int main() {
    std::bitset<8> a(0x5A), b(std::string("00001111"));
    std::bitset<8> c = a & b, d = a | b, e = a ^ b;
    std::printf("%s %s %s %s\n", a.to_string().c_str(), c.to_string().c_str(), d.to_string().c_str(), e.to_string().c_str());
    std::printf("%d %d %d %d %d\n", (int)a.count(), (int)a.test(1), (int)a.any(), (int)std::bitset<8>().none(), (int)b.all());
    a <<= 3;
    std::printf("%s %s %s\n", a.to_string().c_str(), (a >> 5).to_string().c_str(), (~a).to_string().c_str());
    std::bitset<100> big;
    big.set(99);
    big.set(64);
    big.set(0);
    std::bitset<100> shifted = big << 1;
    std::printf("%d %d %d %d\n", (int)big.count(), (int)shifted.test(65), (int)shifted.test(99), (int)(big >> 64).test(35));
    big[3] = true;
    big[0].flip();
    std::printf("%d %d %d\n", (int)big[3], (int)big[0], (int)(big == big));
    try {
        big.to_ulong();
    } catch (const std::overflow_error &) {
        std::printf("overflow\n");
    }
    try {
        a.set(8);
    } catch (const std::out_of_range &) {
        std::printf("out of range\n");
    }
    try {
        std::bitset<4> bad(std::string("10x1"));
    } catch (const std::invalid_argument &) {
        std::printf("invalid\n");
    }
    std::bitset<12> f("101", 3);
    const unsigned long fv = f.to_ulong();
    std::printf("%lu %llu %s\n", fv, std::bitset<70>(0xFFFFFFFFFFULL).to_ullong(), f.flip(0).to_string('.', '#').c_str());
    std::istringstream in("110102");
    std::bitset<6> g;
    in >> g;
    std::cout << g << " " << g.size() << std::endl;
    std::hash<std::bitset<8> > h;
    std::printf("%d\n", (int)(h(a) == h(std::bitset<8>(a.to_ulong()))));
    return 0;
}
