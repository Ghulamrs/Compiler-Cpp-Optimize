#include <iostream>
#include <sstream>
#include <iomanip>
#include <locale>
#include <codecvt>
#include <string>
#include <ctime>

struct Euro : std::moneypunct<char> {
    char do_decimal_point() const { return ','; }
    char do_thousands_sep() const { return '.'; }
    std::string do_grouping() const { return "\3"; }
    std::string do_curr_symbol() const { return "EUR "; }
    int do_frac_digits() const { return 2; }
};

int main() {
    std::locale loc;
    std::cout << std::isalpha('a', loc) << std::isdigit('a', loc) << std::isspace(' ', loc) << std::toupper('x', loc)
              << std::tolower(L'Q', loc) << std::isblank('\t', loc) << '\n';

    std::wstring_convert<std::codecvt_utf8<wchar_t> > u8;
    const std::string bytes = u8.to_bytes(L"A\x00e9\x20ac");
    std::cout << bytes.size() << ' ' << (int)(unsigned char)bytes[1] << ' ' << u8.from_bytes(bytes).size() << '\n';
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> u16;
    const std::u16string w16 = u16.from_bytes("\xF0\x9F\x98\x80z");
    std::cout << w16.size() << ' ' << std::hex << (unsigned)w16[0] << std::dec << ' ' << u16.to_bytes(w16).size() << '\n';
    try {
        u8.from_bytes("\xFF");
        std::cout << "no throw\n";
    } catch (const std::range_error &) {
        std::cout << "range_error\n";
    }
    std::wstring_convert<std::codecvt_utf8<wchar_t> > soft("?", L"!");
    std::cout << soft.to_bytes(L"ok") << ' ' << (soft.from_bytes("\xC3") == L"!") << '\n';

    std::tm t = std::tm();
    t.tm_year = 124; t.tm_mon = 1; t.tm_mday = 29; t.tm_hour = 13; t.tm_min = 5; t.tm_sec = 9; t.tm_wday = 4;
    std::ostringstream tp;
    tp << std::put_time(&t, "%Y-%m-%d %H:%M:%S %a %b");
    std::cout << tp.str() << '\n';
    std::tm r = std::tm();
    std::istringstream tg("2023-07-04 08:30:00 Tuesday");
    tg >> std::get_time(&r, "%Y-%m-%d %H:%M:%S %A");
    std::cout << r.tm_year << ' ' << r.tm_mon << ' ' << r.tm_mday << ' ' << r.tm_hour << ' ' << r.tm_min << ' ' << r.tm_wday
              << ' ' << tg.fail() << '\n';

    std::locale eu(std::locale::classic(), new Euro);
    std::ostringstream me;
    me.imbue(eu);
    me << std::showbase << std::put_money(1234567.0L);
    std::cout << me.str() << '\n';
    std::istringstream mg("EUR 1.234,56");
    mg.imbue(eu);
    long double units = 0;
    mg >> std::showbase >> std::get_money(units);
    std::cout << units << ' ' << mg.fail() << '\n';

    const std::messages<char> &msgs = std::use_facet<std::messages<char> >(loc);
    std::cout << msgs.open("none", loc) << ' ' << msgs.get(0, 1, 2, "default") << '\n';
    std::cout << std::use_facet<std::time_get<char> >(loc).date_order() << '\n';
    return 0;
}
