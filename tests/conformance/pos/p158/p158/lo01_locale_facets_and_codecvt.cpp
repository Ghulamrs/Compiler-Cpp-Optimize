#include <locale>
#include <cstdio>

struct Comma : std::numpunct<char> {
    char do_decimal_point() const { return ','; }
    std::string do_grouping() const { return "\3"; }
};

int main() {
    std::locale c = std::locale::classic();
    std::locale g;
    const std::ctype<char> &ct = std::use_facet<std::ctype<char> >(c);
    const std::ctype<wchar_t> &wct = std::use_facet<std::ctype<wchar_t> >(g);
    std::printf("%d %d %c %c %d\n", (int)ct.is(std::ctype_base::digit, '7'), (int)ct.is(std::ctype_base::alpha, '7'),
                ct.toupper('q'), ct.widen('z'), (int)wct.is(std::ctype_base::space, L' '));
    std::locale mine(c, new Comma);
    const std::numpunct<char> &np = std::use_facet<std::numpunct<char> >(mine);
    std::printf("%c %d [%s] %s %s\n", np.decimal_point(), (int)np.grouping()[0], std::use_facet<std::numpunct<char> >(c).truename().c_str(),
                mine.name().c_str(), c.name().c_str());
    std::printf("%d %d %d\n", (int)(mine == c), (int)(g == c), (int)std::has_facet<std::collate<char> >(mine));
    const std::collate<char> &co = std::use_facet<std::collate<char> >(mine);
    const char a[] = "apple", b[] = "apricot";
    std::printf("%d %d\n", co.compare(a, a + 5, b, b + 7), (int)mine(std::string("b"), std::string("a")));
    std::mbstate_t st = std::mbstate_t();
    const std::codecvt<char32_t, char, std::mbstate_t> &cv = std::use_facet<std::codecvt<char32_t, char, std::mbstate_t> >(c);
    const char32_t in[] = { 0x41, 0xE9, 0x20AC, 0x1F600 };
    const char32_t *next;
    char out[16];
    char *to;
    cv.out(st, in, in + 4, next, out, out + 16, to);
    std::printf("%d bytes\n", (int)(to - out));
    char32_t back[4];
    const char *from;
    char32_t *bt;
    cv.in(st, out, to, from, back, back + 4, bt);
    std::printf("%d %x %x\n", (int)(bt - back), (unsigned)back[2], (unsigned)back[3]);
    std::locale prev = std::locale::global(mine);
    std::locale now;
    std::printf("%d %s\n", (int)(now == mine), prev.name().c_str());
    std::locale::global(prev);
    return 0;
}
