#include <iostream>
#include <regex>
#include <string>

static void tryPattern(const char *p, std::regex::flag_type f = std::regex::ECMAScript) {
    try {
        std::regex r(p, f);
        std::cout << "ok ";
    } catch (const std::regex_error &e) {
        std::cout << e.code() << ' ';
    }
}

int main() {
    tryPattern("(ab");
    tryPattern("ab)");
    tryPattern("[ab");
    tryPattern("a{3,2}");
    tryPattern("*a");
    tryPattern("a\\");
    tryPattern("[b-a]");
    tryPattern("[[:nonsense:]]");
    tryPattern("\\2(a)");
    tryPattern("a{2}");
    std::cout << '\n';

    std::regex_traits<char> tr;
    const char name[] = "digit";
    const std::regex_traits<char>::char_class_type d = tr.lookup_classname(name, name + 5);
    std::cout << tr.isctype('7', d) << tr.isctype('x', d) << ' ' << tr.value('f', 16) << ' ' << tr.value('9', 8) << ' '
              << tr.translate_nocase('Q') << ' ' << std::regex_traits<char>::length("abc") << '\n';

    std::regex r;
    r.assign("b+", std::regex::extended | std::regex::icase);
    std::cout << std::regex_search("aBBc", r) << ' ' << ((r.flags() & std::regex::icase) != 0) << ' ' << r.mark_count() << ' ';
    r = "(x)(y)";
    std::cout << r.mark_count() << ' ';
    std::regex other("z");
    r.swap(other);
    std::cout << r.mark_count() << ' ' << other.mark_count() << ' ' << (r.getloc() == std::locale()) << '\n';
    std::regex fromString(std::string("a|b"));
    std::regex fromList({ 'c', '+' });
    std::cout << std::regex_match("b", fromString) << std::regex_match("ccc", fromList) << '\n';
    return 0;
}
