#include <cstdio>
namespace very_long_name { namespace inner { int v = 2; int f() { return 3; } } int w = 5; }
namespace ln { int v = 40; }
int main() {
    int r = ln::v;
    {
        namespace ln = very_long_name::inner;
        r += ln::v + ln::f();
        {
            namespace vl = very_long_name;
            using namespace vl;
            r += w;
        }
    }
    r += ln::v;
    std::printf("%d\n", r);
    return 0;
}
