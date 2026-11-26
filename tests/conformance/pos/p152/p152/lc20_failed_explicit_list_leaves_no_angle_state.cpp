#include <string>
#include <sstream>
#include <cstdio>
template <std::size_t N> struct B {
    template <class C, class Tr, class A> std::basic_string<C, Tr, A> str(C z = C('0')) const { return std::basic_string<C, Tr, A>(N, z); }
    template <class C, class Tr> std::basic_string<C, Tr, std::allocator<C> > str(C z = C('0')) const { return str<C, Tr, std::allocator<C> >(z); }
    template <class C> std::basic_string<C, std::char_traits<C>, std::allocator<C> > str(C z = C('0')) const { return str<C, std::char_traits<C>, std::allocator<C> >(z); }
    std::string str(char z = '0') const { return str<char, std::char_traits<char>, std::allocator<char> >(z); }
};
template <std::size_t N> std::istream &operator>>(std::istream &is, B<N> &) {
    std::string s;
    is >> std::ws;
    is >> s;
    return is;
}
int main() {
    B<3> b;
    std::istringstream in("xx");
    in >> b;
    std::printf("%s\n", b.str().c_str());
    std::printf("ok\n");
}
