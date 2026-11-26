#include <cstdio>
#include <typeinfo>
struct B { virtual ~B() {} };
int main() {
    B *none = 0;
    try {
        const std::type_info &t = typeid(*none);
        (void)t;
        std::printf("no throw\n");
    } catch (std::bad_typeid &x) {
        std::printf("bad_typeid: %s\n", x.what());
    }
    std::bad_cast c;
    std::bad_typeid t;
    const std::exception &ec = c;
    const std::exception &et = t;
    std::printf("%s | %s\n", ec.what(), et.what());
    try { throw std::bad_cast(); } catch (std::exception &x) { std::printf("as exception: %s\n", x.what()); }
    try { throw std::bad_typeid(); } catch (std::bad_typeid &x) { std::printf("as itself: %s\n", x.what()); }
    return 0;
}
