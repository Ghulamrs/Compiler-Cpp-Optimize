#include <cstdio>
#include <string>

// [temp.inst]/1: the typedef names a specialization of a template only declared so far; once the template is
// defined, a use needing the class complete instantiates it.
template <class T, class U = int> class Box;
typedef Box<char> CharBox;
template <class C> struct Named;
typedef Named<char> NamedChar;

template <class T, class U> class Box {
public:
    T t;
    U u;
    int get() const { return static_cast<int>(sizeof(T)) + static_cast<int>(sizeof(U)); }
};
template <class C> struct Named {
    explicit Named(const std::string &s) : n(static_cast<unsigned>(s.size())) {}
    unsigned n;
};

int main() {
    CharBox b;
    NamedChar nc("abc");
    CharBox *p = &b;
    std::printf("%d %u %d\n", b.get(), nc.n, p->get());
    return 0;
}
