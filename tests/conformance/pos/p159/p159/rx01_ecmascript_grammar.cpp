#include <iostream>
#include <regex>
#include <string>

static void show(const char *pattern, const char *text) {
    std::cmatch m;
    const bool found = std::regex_search(text, m, std::regex(pattern));
    std::cout << pattern << " ~ " << text << ": ";
    if (!found) {
        std::cout << "no\n";
        return;
    }
    for (std::size_t i = 0; i < m.size(); i++) std::cout << (m[i].matched ? "[" + m[i].str() + "]" : std::string("-"));
    std::cout << " @" << m.position(0) << '\n';
}

int main() {
    show("a+?", "aaa");
    show("a{2,3}", "aaaa");
    show("a{2,}?b", "aaaab");
    show("(a|ab)(c|bcd)(d*)", "abcd");
    show("((a)|b)+", "ab");
    show("(z)((a+)?(b+)?(c))*", "zaacbbbcac");
    show("\\bfoo\\b", "a foo b");
    show("\\Bo\\B", "foo");
    show("q(?=u)", "quit");
    show("q(?!u)i", "qi");
    show("(\\d+)-\\1", "12-12 3-4");
    show("[^aeiou\\s]+", "aei xyz");
    show("[\\d.]+", "v1.25!");
    show("[[:alpha:]][[:digit:]]", "--x9--");
    show("\\x41\\u0042\\t", "AB\t");
    show("a.c", "a\nc abc");
    show("(?:ab)+", "ababx");
    show("^\\w+$", "word");
    show("x*", "aaa");
    show("[a-c-]+", "-ab-");
    show("\\cJ", "a\nb");
    return 0;
}
