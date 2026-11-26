#include <cstdio>

// [temp.deduct.type]/8: an argument the parameter writes out must be the argument's exactly. `Out<char, Tr>`
// deduces nothing from an Out<wchar_t, ...>, and nothing is made from it.
template <class C> struct Traits { typedef C char_type; };
template <class C, class Tr = Traits<C> > struct Out {
    int last;
    Out() : last(0) {}
    // Only a well-formed specialization can be made: one whose traits are its own character's.
    typename Tr::char_type check[sizeof(typename Tr::char_type) == sizeof(C) ? 1 : -1];
};

template <class C, class Tr> int put(Out<C, Tr> &o, C) { return o.last = 1; }
template <class C, class Tr> int put(Out<C, Tr> &o, char) { return o.last = 2; }
template <class Tr> int put(Out<char, Tr> &o, char) { return o.last = 3; }

int main() {
    Out<char> n;
    Out<wchar_t> w;
    const int a = put(n, 'x');
    const int b = put(w, 'x');
    const int c = put(w, L'x');
    std::printf("%d %d %d\n", a, b, c);
    return 0;
}
