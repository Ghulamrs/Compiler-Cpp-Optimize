#include <cstdio>
#include <cctype>
#include <string>
#include <vector>
#include <bitset>
#include <stdexcept>
#include <utility>
struct ci_traits : std::char_traits<char> {
    static bool eq(char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }
    static bool lt(char a, char b) { return std::tolower((unsigned char)a) < std::tolower((unsigned char)b); }
    static int compare(const char *a, const char *b, std::size_t n) {
        for (std::size_t i = 0; i < n; i++) {
            if (lt(a[i], b[i])) return -1;
            if (lt(b[i], a[i])) return 1;
        }
        return 0;
    }
    static const char *find(const char *s, std::size_t n, char c) {
        for (std::size_t i = 0; i < n; i++)
            if (eq(s[i], c)) return s + i;
        return 0;
    }
};
typedef std::basic_string<char, ci_traits> ci_string;
static int allocs = 0, frees = 0;
template <class T> struct Count {
    typedef T value_type;
    Count() {}
    template <class U> Count(const Count<U> &) {}
    T *allocate(std::size_t n) { ++allocs; return static_cast<T *>(::operator new(n * sizeof(T))); }
    void deallocate(T *p, std::size_t) { ++frees; ::operator delete(p); }
};
template <class T, class U> bool operator==(const Count<T> &, const Count<U> &) { return true; }
template <class T, class U> bool operator!=(const Count<T> &, const Count<U> &) { return false; }
typedef std::basic_string<char, std::char_traits<char>, Count<char> > cstring;
int main() {
    ci_string a("Hello World"), b("HELLO world");
    std::printf("%d %d %d %d\n", (int)(a == b), (int)a.find("WORLD"), (int)a.find('o', 5), (int)(a.compare("hello worle") < 0));
    std::printf("%d %d\n", (int)a.find_first_of("XYZW"), (int)a.rfind('L'));
    {
        cstring s("a string long enough to leave the inline buffer behind");
        cstring t(s);
        t += s;
        t.erase(10, 20);
        cstring u(std::move(t));
        std::printf("%d %d %d\n", (int)s.size(), (int)u.size(), allocs > 0);
    }
    std::printf("%d\n", allocs == frees);
    std::string s = "abcdefabc";
    std::printf("%d %d %d %d %d\n", (int)s.find("abc", 1), (int)s.find("", 9), (int)s.find("", 10) == -1, (int)s.rfind("abc", 5), (int)s.rfind(""));
    std::printf("%d %d %d %d\n", (int)s.find_first_of("fed", 0, 2), (int)s.find_last_of('a', 5), (int)s.find_first_not_of("abc", 1), (int)s.find_last_not_of(std::string("cb")));
    s.insert(s.begin() + 1, 3, 'X');
    s.insert(s.end(), {'1', '2'});
    s.replace(s.begin(), s.begin() + 2, 4, 'Z');
    s.replace(s.end() - 2, s.end(), {'!', '?'});
    s.erase(s.begin() + 4);
    std::printf("%s\n", s.c_str());
    char buf[8] = {0};
    std::size_t got = s.copy(buf, 5, 3);
    std::printf("%d %s %d\n", (int)got, buf, s.compare(0, 3, "ZZZ"));
    const char *names[] = {"substr", "erase", "insert", "compare", "at", "append", "replace"};
    for (int k = 0; k < 7; k++) {
        try {
            switch (k) {
            case 0: s.substr(100); break;
            case 1: s.erase(100); break;
            case 2: s.insert(100, "x"); break;
            case 3: s.compare(100, 1, "x"); break;
            case 4: s.at(100); break;
            case 5: s.append(std::string("abc"), 9, 1); break;
            case 6: s.replace(100, 1, "x"); break;
            }
            std::printf("%s: none\n", names[k]);
        } catch (const std::out_of_range &) {
            std::printf("%s: out_of_range\n", names[k]);
        }
    }
    std::string x("x"), y("y");
    std::string z = std::string("a") + x + "b" + std::move(y) + 'c';
    std::string w = 'd' + std::string("e") + std::string("f");
    std::printf("%s %s %d\n", z.c_str(), w.c_str(), (int)("abc" < std::string("abd")));
    std::vector<std::string> v = {"one", "two"};
    std::string il({'i', 'l'});
    std::string il2({'a', 'b', 'c'}, std::allocator<char>());
    std::printf("%s %s %s %s\n", v[0].c_str(), v[1].c_str(), il.c_str(), il2.c_str());
    std::bitset<6> bits(std::string("xx101101"), 2);
    std::printf("%s %s %s\n", bits.to_string().c_str(), bits.to_string<char>('.', '#').c_str(),
                bits.to_string<char, std::char_traits<char>, std::allocator<char> >().c_str());
    std::wstring ws(L"wide");
    std::printf("%d %d\n", (int)ws.size(), (int)(ws.find(L"de") == 2));
    s.clear();
    s.resize(3, 'q');
    s.shrink_to_fit();
    std::printf("%s %d %d\n", s.c_str(), (int)s.empty(), (int)(s.capacity() >= 3));
    return 0;
}
