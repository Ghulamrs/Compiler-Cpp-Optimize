// std::wstring - the same basic_string<C> as std::string, over wchar_t, and
// the operators around it deduced from the character type. wchar_t is 4 bytes
// on the Itanium targets and 2 on x86_64-windows and the C6000, so nothing
// here prints a sizeof; code units are printed as numbers, never with %ls.
#include <string>
extern "C" int printf(const char *, ...);

static void show(const std::wstring &w) {
    printf("%d:", (int)w.size());
    for (std::wstring::size_type i = 0; i < w.size(); i++) printf(" %d", (int)w[i]);
    printf("\n");
}

int main() {
    std::wstring a = L"ab";
    std::wstring b(L"cd", 1);
    std::wstring c(3, L'z');
    show(a); show(b); show(c);
    a += L"xy";
    a += L'!';
    a += b;
    show(a);
    std::wstring d = a + L"-" + c + L'.';
    show(d);
    std::wstring e = L"<" + d;
    show(e);
    printf("%d %d %d %d\n", (int)(a == a), (int)(a == L"abxy!c"), (int)(L"abxy!c" == a), (int)(a != b));
    printf("%d %d\n", (int)(b < c), (int)(c > b));
    printf("%d %d %d\n", (int)d.find(L'-'), (int)d.find(L"zzz"), (int)(d.find(L"q") == std::wstring::npos));
    std::wstring s = d.substr(1, 3);
    show(s);
    s.insert(1, L"++");
    show(s);
    s.erase(0, 2);
    show(s);
    std::wstring u = L"é€";
    show(u);
    std::wstring n = std::to_wstring(-1234);
    show(n);
    std::string plain = "still narrow";
    printf("%s %d\n", plain.c_str(), (int)plain.size());
    std::wstring copy(u);
    copy = a;
    show(copy);
    copy.clear();
    printf("%d %d\n", (int)copy.empty(), (int)copy.length());
    return 0;
}
