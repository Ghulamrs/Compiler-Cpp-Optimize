#include <iostream>
#include <streambuf>
#include <string>

// Upper-cases what is written through it, one character at a time.
class Upper : public std::streambuf {
public:
    std::string got;
protected:
    int_type overflow(int_type c) {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        const char ch = traits_type::to_char_type(c);
        got += ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
        return c;
    }
};

// Reads a fixed text through a get area it sets once.
class Source : public std::streambuf {
    char text_[16];
public:
    Source() {
        const char t[] = "17 words";
        for (int i = 0; i < 9; i++) text_[i] = t[i];
        setg(text_, text_, text_ + 8);
    }
};

int main() {
    Upper u;
    std::ostream out(&u);
    out << "abc" << 12 << ' ' << 3.5;
    std::cout << '[' << u.got << "]\n";

    Source s;
    std::istream in(&s);
    int n = 0;
    std::string w;
    in >> n >> w;
    std::cout << n << ' ' << w << " eof " << in.eof() << '\n';

    std::ostream redirected(std::cout.rdbuf());
    redirected << "through cout's buffer\n";
    std::streambuf *was = std::cout.rdbuf(&u);
    std::cout << "captured";
    std::cout.rdbuf(was);
    std::cout << '[' << u.got << "]\n";
    return 0;
}
