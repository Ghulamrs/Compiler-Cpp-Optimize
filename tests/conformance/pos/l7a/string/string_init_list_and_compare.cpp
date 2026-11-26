#include <cstdio>
#include <initializer_list>
#include <string>
int main() {
    std::string a = "ab";
    std::string b = "xyz";
    std::printf("[%s] %d [%s]\n", a.c_str(), (int)a.size(), b.c_str());
    std::initializer_list<char> cd = {'c', 'd'};
    a += cd;
    std::initializer_list<char> e = {'e'};
    a.append(e);
    std::printf("[%s]\n", a.c_str());
    std::initializer_list<char> qr = {'q', 'r'};
    b.assign(qr);
    std::printf("[%s] ", b.c_str());
    std::initializer_list<char> one = {'s'};
    b = one;
    std::printf("[%s]\n", b.c_str());
    std::string m = "mid";
    std::printf("%d %d %d %d %d %d %d %d\n",
        (int)(m < "nab"), (int)("aaa" < m), (int)(m > "mi"), (int)("z" > m),
        (int)(m <= "mid"), (int)("mid" <= m), (int)(m >= "mie"), (int)("mic" >= m));
    std::wstring w = L"wide";
    std::printf("%d %d\n", (int)(w < L"wider"), (int)(L"a" < w));
    return 0;
}
