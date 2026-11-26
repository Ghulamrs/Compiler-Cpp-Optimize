#include <cstdio>
#include <string>
#include <type_traits>
int main() {
    typedef std::char_traits<char> T;
    char buf[8] = "";
    T::copy(buf, "hello", 6);
    T::move(buf + 1, buf, 3);
    const char *hit = T::find(buf, 5, 'l');
    std::printf("%d %d %d %s %d\n", T::eof(), (int)T::length("four"), T::compare("abc", "abd", 3) < 0, buf, (int)(hit - buf));
    std::printf("%d %d %d %d %d\n", (int)T::lt('a', '\xe9'), (int)T::eq('q', 'q'), T::to_int_type('\xff'), (int)T::eq_int_type(T::eof(), T::not_eof(T::eof())), T::not_eof('x'));
    T::assign(buf, 3, 'z');
    char one = 0;
    T::assign(one, 'k');
    std::printf("%s %c %c\n", buf, one, T::to_char_type(65));
    typedef std::char_traits<char16_t> T16;
    typedef std::char_traits<char32_t> T32;
    std::printf("%d %d %u %u\n", (int)T16::length(u"abc"), (int)T32::length(U"abcd"), (unsigned)T16::eof(), (unsigned)T32::eof());
    std::printf("%d %d %d\n",
        (int)std::is_same<T::int_type, int>::value,
        (int)std::is_same<T16::char_type, char16_t>::value,
        (int)std::is_same<std::u32string::traits_type, T32>::value);
    std::u16string u = u"grid";
    u += u'e';
    std::u16string v = u.substr(1, 3);
    std::u32string w(3, U'z');
    w.append(U"ap");
    std::printf("%d %d %d %d %d %d\n", (int)u.size(), (int)(v == u"rid"), (int)u.find(u'd'), (int)w.size(), (int)(w.compare(U"zzzap") == 0), (int)(u < std::u16string(u"gs")));
    std::hash<std::string> hs;
    std::hash<std::u16string> h16;
    std::hash<std::u32string> h32;
    std::hash<std::wstring> hw;
    std::string a = "same", b = "same", c = "other";
    std::printf("%d %d %d %d %d\n", (int)(hs(a) == hs(b)), (int)(hs(a) != hs(c)), (int)(h16(u) == h16(std::u16string(u"gride"))), (int)(h32(w) == h32(w)), (int)(hw(L"x") == hw(std::wstring(L"x"))));
    std::printf("%d\n", (int)std::is_same<std::hash<std::string>::result_type, std::size_t>::value);
    return 0;
}
