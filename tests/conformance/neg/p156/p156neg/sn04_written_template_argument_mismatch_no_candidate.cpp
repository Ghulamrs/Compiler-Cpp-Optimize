// [temp.deduct.type]/8: Out<char, Tr> deduces nothing from an Out<wchar_t, ...>, so no put is viable.
template <class C> struct Traits {};
template <class C, class Tr = Traits<C> > struct Out {};
template <class Tr> int put(Out<char, Tr> &, char) { return 3; }

int main() {
    Out<wchar_t> w;
    return put(w, 'x');
}
