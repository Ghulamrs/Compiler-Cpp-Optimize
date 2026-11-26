// The shipped <string>: construction, concatenation, compare, find, substr, c_str, size/capacity growth, iteration, +=, operator[], at() throwing, std::to_string, stoi, string literal comparisons, swap.
#include <cstdio>
#include <string>
#include <stdexcept>
int main() {
    std::string a = "hello", b("world"), c(3, 'x'), d;
    std::string e = a + " " + b + '!';
    std::printf("%s %d %d %s %d\n", e.c_str(), (int)e.size(), (int)d.empty(), c.c_str(), (int)d.size());
    std::printf("%d %d %d\n", a == "hello", a < b, a.compare(b) < 0);
    std::printf("%d %d %d\n", (int)e.find("wor"), (int)e.find('z') == (int)std::string::npos, (int)e.rfind('l'));
    std::printf("%s %s\n", e.substr(6, 5).c_str(), e.substr(6).c_str());
    d += "ab"; d += 'c'; d += a; std::printf("%s %c %c\n", d.c_str(), d[0], d.at(2));
    try { d.at(100); std::printf("no\n"); } catch (const std::out_of_range &) { std::printf("oor\n"); }
    std::string big; for (int i = 0; i < 1000; ++i) big += "0123456789"; std::printf("%d %d\n", (int)big.size(), big[9999] == '9');
    int count = 0; for (char ch : a) if (ch == 'l') ++count; std::printf("%d\n", count);
    a.swap(b); std::printf("%s %s\n", a.c_str(), b.c_str());
    std::printf("%s %d %g\n", std::to_string(42).c_str(), std::stoi("123"), std::stod("2.5"));
    std::string f = "abc"; f.insert(1, "XY"); f.erase(0, 1); f.replace(0, 2, "--"); std::printf("%s\n", f.c_str());
    std::string g = "a,b,c"; size_t pos = 0; int parts = 0; while ((pos = g.find(',', pos)) != std::string::npos) { ++parts; ++pos; } std::printf("%d\n", parts);
    std::printf("%d %d\n", (int)std::string("x").length(), std::string("ab").append("cd") == "abcd");
    const char *cs = e.c_str(); std::printf("%d\n", cs[e.size()] == 0);
    std::string h = e; h[0] = 'H'; std::printf("%c %c\n", e[0], h[0]);
    return 0;
}
