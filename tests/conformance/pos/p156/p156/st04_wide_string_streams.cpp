#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>

static std::string narrow(const std::wstring &w) {
    std::string s;
    for (std::wstring::size_type i = 0; i < w.size(); i++) s += static_cast<char>(w[i]);
    return s;
}

int main() {
    std::wostringstream wo;
    wo << L"w" << 7 << L' ' << 1.5 << L' ' << std::hex << 255 << std::dec << L' ' << std::setw(4) << std::setfill(L'.') << 3
       << L' ' << std::boolalpha << true << ' ' << "narrow";
    std::cout << '[' << narrow(wo.str()) << "]\n";

    std::wistringstream wi(L"42 word 2.25\nnext");
    int n = 0;
    std::wstring word;
    double d = 0;
    wi >> n >> word >> d;
    std::wstring line;
    std::getline(wi, line);
    std::getline(wi, line);
    std::cout << n << ' ' << narrow(word) << ' ' << d << " [" << narrow(line) << "]\n";

    std::wstringstream ws;
    ws << 10 << L' ' << 20;
    int a = 0, b = 0;
    ws >> a >> b;
    std::cout << a + b << '\n';
    return 0;
}
