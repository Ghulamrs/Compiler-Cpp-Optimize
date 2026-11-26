#include <iostream>
#include <sstream>
#include <string>

int main() {
    std::istringstream in("12 abc 3.5\nsecond line\n  -7 0x1f 017 tail");
    int i = 0;
    std::string w;
    double d = 0;
    in >> i >> w >> d;
    std::cout << i << ' ' << w << ' ' << d << '\n';
    std::string line;
    std::getline(in, line);
    std::cout << "rest of line [" << line << "]\n";
    std::getline(in, line);
    std::cout << "line [" << line << "]\n";
    int n = 0, h = 0, o = 0;
    in >> n >> std::hex >> h >> std::oct >> o >> std::dec;
    std::cout << n << ' ' << h << ' ' << o << '\n';
    std::string last;
    in >> last;
    std::cout << "last [" << last << "] eof " << in.eof() << '\n';
    in >> last;
    std::cout << "after end fail " << in.fail() << '\n';

    std::istringstream bad("x 5");
    int k = 9;
    bad >> k;
    std::cout << "bad " << bad.fail() << ' ' << k << '\n';
    bad.clear();
    char c = 0;
    bad >> c >> k;
    std::cout << "recovered " << c << ' ' << k << '\n';

    std::istringstream parts("a,b,,c");
    std::string part;
    while (std::getline(parts, part, ',')) std::cout << '<' << part << '>';
    std::cout << '\n';

    std::istringstream raw("  hi\tthere");
    raw >> std::noskipws;
    char r1 = 0, r2 = 0;
    raw >> r1 >> r2;
    std::cout << "noskipws [" << r1 << r2 << "]\n";
    raw >> std::skipws >> w;
    const char got = static_cast<char>(raw.get());
    const char next = static_cast<char>(raw.peek());
    std::cout << "word " << w << " get " << (got == '\t' ? "tab" : "other") << " peek " << next << '\n';
    char buf[8];
    raw.read(buf, 3);
    std::cout << "read " << raw.gcount() << ' ' << std::string(buf, 3) << '\n';
    raw.ignore(10, 'e');
    std::cout << "ignored, eof " << raw.eof() << '\n';
    return 0;
}
