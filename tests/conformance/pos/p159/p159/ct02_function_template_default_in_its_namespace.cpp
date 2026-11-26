#include <cstdio>
#include <string>

// [dcl.fct.default]/9: a default argument's names are bound where it is written - k::d is n's however far from n
// the template is called, and the same name in the caller's namespace is not it.
namespace n {
namespace k { enum f { d = 3 }; }
const int base = 10;
template <class T> struct Box { T t; };
template <class T> int g(const T &, k::f x = k::d) { return x; }
template <class C, class A> int h(const std::basic_string<C> &, Box<A> &, int y = base + k::d) { return y; }
}
namespace other {
namespace k { enum f { d = 100 }; }
const int base = 500;
int use() {
    n::Box<int> b;
    return n::g(1) + n::h(std::string("a"), b);
}
}

int main() {
    std::printf("%d\n", other::use());
    return 0;
}
