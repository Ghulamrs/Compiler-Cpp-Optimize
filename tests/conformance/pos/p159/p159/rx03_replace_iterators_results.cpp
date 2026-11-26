#include <iostream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

int main() {
    const std::string s = "2024-01-31 and 1999-12-25";
    const std::regex date("(\\d{4})-(\\d{2})-(\\d{2})");
    std::cout << std::regex_replace(s, date, "$3/$2/$1") << '\n';
    std::cout << std::regex_replace(s, date, "[$&]", std::regex_constants::format_first_only) << '\n';
    std::cout << std::regex_replace(s, date, "<$1>", std::regex_constants::format_no_copy) << '\n';
    std::cout << std::regex_replace(s, date, "\\3.\\2.\\1 &", std::regex_constants::format_sed) << '\n';
    std::cout << std::regex_replace(std::string("a.b.c"), std::regex("\\."), "$$") << ' '
              << std::regex_replace(std::string("abc"), std::regex("b"), "[$`|$']") << '\n';
    std::string out;
    std::regex_replace(std::back_inserter(out), s.begin(), s.end(), date, "$1");
    std::cout << out << '\n';

    const std::string t = "baaa";
    const std::regex as("a*");
    for (std::sregex_iterator i(t.begin(), t.end(), as), e; i != e; ++i)
        std::cout << i->position() << ':' << i->length() << ' ';
    std::cout << '\n';
    const std::string csv = "a,b,,c";
    const std::regex comma(",");
    std::vector<std::string> parts(std::sregex_token_iterator(csv.begin(), csv.end(), comma, -1), std::sregex_token_iterator());
    std::cout << parts.size();
    for (std::size_t i = 0; i < parts.size(); i++) std::cout << '[' << parts[i] << ']';
    std::cout << '\n';
    const std::string kv = "x=1;y=22;z=333";
    std::regex pair("(\\w)=(\\d+)");
    const int both[] = { 2, 1 };
    for (std::sregex_token_iterator i(kv.begin(), kv.end(), pair, both), e; i != e; ++i) std::cout << *i << ' ';
    std::cout << '\n';

    std::smatch m;
    std::regex_search(s, m, date);
    std::cout << m.ready() << ' ' << m.size() << ' ' << m.prefix().str().size() << ' ' << m.suffix().str() << ' ' << m.length(1)
              << ' ' << m.position(2) << ' ' << m.format("$2-$1") << ' ' << (m[1] < m[3]) << (m[1] == "2024") << '\n';
    std::smatch copy = m;
    std::cout << (copy == m) << ' ' << std::distance(m.begin(), m.end()) << '\n';
    return 0;
}
