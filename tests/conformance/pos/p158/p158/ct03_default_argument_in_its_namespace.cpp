#include <cstdio>

// [temp.param]: a default template argument is read where it was written - (mode)0 names n's mode however far
// from n the template is used.
namespace n {
enum mode { none = 0, one = 1, two = 2 };
const int base = 40;
template <class E, mode M = (mode)0, int K = base + 2> struct S {
    static int value() { return static_cast<int>(M) * 100 + K; }
};
}
namespace other {
const int base = 7;
enum mode { none = 5 };
int use() { return n::S<int>::value() + n::S<char, n::two>::value(); }
}

int main() {
    std::printf("%d %d\n", n::S<long>::value(), other::use());
    return 0;
}
