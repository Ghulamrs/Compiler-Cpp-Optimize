#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>

struct German : std::numpunct<char> {
    char do_decimal_point() const { return ','; }
    char do_thousands_sep() const { return '.'; }
    std::string do_grouping() const { return "\3"; }
    std::string do_truename() const { return "ja"; }
    std::string do_falsename() const { return "nein"; }
};

int main() {
    std::locale de(std::locale::classic(), new German);
    std::ostringstream o;
    o.imbue(de);
    o << 1234567 << ' ' << -1234 << ' ' << 1234.5 << ' ' << std::fixed << std::setprecision(2) << 9876543.25 << ' '
      << std::boolalpha << true << ' ' << false << ' ' << 12 << ' ' << std::setw(10) << 12345;
    std::cout << '[' << o.str() << "]\n";

    std::istringstream in("1.234.567 2,5 ja nein 12.34");
    in.imbue(de);
    long n = 0;
    double d = 0;
    bool a = false, b = true;
    in >> n >> d >> std::boolalpha >> a >> b;
    std::cout << n << ' ' << d << ' ' << a << ' ' << b << ' ' << in.good() << '\n';
    long bad = 0;
    in >> bad;
    std::cout << "misgrouped fail " << in.fail() << ' ' << bad << '\n';

    std::cout << (o.getloc() == de) << ' ' << o.getloc().name() << ' ' << std::cout.getloc().name() << '\n';
    std::wostringstream w;
    w << 1234567 << L' ' << 2.5;
    std::cout << w.str().size() << '\n';
    return 0;
}
