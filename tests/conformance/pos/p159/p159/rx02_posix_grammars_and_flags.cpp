#include <iostream>
#include <regex>
#include <string>

static std::string first(const char *pattern, const char *text, std::regex::flag_type f) {
    std::cmatch m;
    if (!std::regex_search(text, m, std::regex(pattern, f))) return "no";
    std::string r = m.str();
    for (std::size_t i = 1; i < m.size(); i++) r += m[i].matched ? "|" + m[i].str() : std::string("|-");
    return r;
}

int main() {
    std::cout << first("a|ab|abc", "abcd", std::regex::extended) << ' ' << first("a|ab|abc", "abcd", std::regex::ECMAScript) << '\n';
    std::cout << first("(a+)(b*)", "aaabbx", std::regex::extended) << '\n';
    std::cout << first("\\(ab\\)*c", "ababc", std::regex::basic) << ' ' << first("a\\{2\\}", "aaa", std::regex::basic) << '\n';
    std::cout << first("\\(a\\)\\1", "xaay", std::regex::basic) << ' ' << first("*a", "x*a", std::regex::basic) << '\n';
    std::cout << first("ab+c?", "abbbc", std::regex::egrep) << ' ' << first("cat\ndog", "hotdog", std::regex::grep) << '\n';
    std::cout << first("[[:upper:]]+", "abCDe", std::regex::extended) << ' ' << first("\\t", "a\tb", std::regex::awk) << '\n';
    std::cout << first("HELLO", "say hello", std::regex::icase) << ' ' << first("[a-z]+", "ABC def", std::regex::icase) << '\n';
    std::regex nos("(a)(b)", std::regex::nosubs);
    std::cmatch m;
    std::regex_search("ab", m, nos);
    std::cout << nos.mark_count() << ' ' << m.size() << '\n';
    std::cout << std::regex_search("abc", std::regex("^b")) << std::regex_search("abc", std::regex("^a"), std::regex_constants::match_not_bol)
              << std::regex_search("abc", std::regex("c$"), std::regex_constants::match_not_eol)
              << std::regex_search("xab", std::regex("ab"), std::regex_constants::match_continuous)
              << std::regex_search("", std::regex("a*"), std::regex_constants::match_not_null) << '\n';
    std::wregex w(L"(\\w+)\\s(\\d+)");
    std::wsmatch wm;
    const std::wstring ws = L"item 42";
    const bool whole = std::regex_match(ws, wm, w);
    std::cout << whole << ' ' << wm[2].length() << ' ' << (wm[1] == L"item") << '\n';
    return 0;
}
