// [temp.expl.spec]/2 and [namespace.def]/8: an explicit specialization written in the enclosing namespace of an
// inline one specializes the inline one's template, never a sibling namespace's of the same name.
#include <cstdio>
namespace lib {
inline namespace v2 { template <class T> int ver(T) { return 2; } struct S {}; }
namespace v1 { template <class T> int ver(T) { return 1; } }
}
namespace lib { template <> int ver(int) { return 22; } }
namespace lib { namespace v1 { template <> int ver(char) { return 11; } } }
template <> int lib::ver(double) { return 23; }
int main() {
    lib::S s; (void)s;
    std::printf("%d %d %d %d %d %d\n", lib::ver(1), lib::ver(1.0), lib::ver('c'), lib::v1::ver(1), lib::v1::ver('c'),
                lib::v2::ver(1));
    return 0;
}
