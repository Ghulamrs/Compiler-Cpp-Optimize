#include <iostream>
#include <strstream>
#include <cstring>
#include <string>

int main() {
    std::ostrstream dyn;
    dyn << "count " << 42 << ' ' << 2.5 << std::ends;
    std::cout << '[' << dyn.str() << "] " << dyn.pcount() << '\n';
    dyn.freeze(false);

    std::ostrstream big;
    for (int i = 0; i < 100; i++) big << i % 10;
    big << std::ends;
    const char *all = big.str();
    std::cout << std::strlen(all) << ' ' << all[0] << all[99] << '\n';
    big.freeze(false);

    char fixed[16] = "hello";
    std::ostrstream app(fixed, sizeof fixed, std::ios_base::app);
    app << " you" << std::ends;
    std::cout << fixed << ' ' << app.pcount() << '\n';

    char small[4];
    std::ostrstream full(small, sizeof small);
    full << "abcdef";
    std::cout << "full bad " << full.bad() << ' ' << full.pcount() << '\n';

    std::istrstream in("12 words 3.5");
    int n = 0;
    std::string w;
    double d = 0;
    in >> n >> w >> d;
    std::cout << n << ' ' << w << ' ' << d << '\n';

    const char raw[] = "abcdef";
    std::istrstream part(raw, 3);
    std::string got;
    part >> got;
    std::cout << got << " eof " << part.eof() << '\n';

    std::strstream both;
    both << 7 << ' ' << 8;
    int a = 0, b = 0;
    both >> a >> b;
    std::cout << a + b << ' ' << both.pcount() << '\n';
    both.freeze(false);

    std::istrstream seekable("0123456789");
    seekable.seekg(4);
    char c = 0;
    seekable >> c;
    std::cout << c << ' ' << seekable.tellg() << '\n';
    return 0;
}
