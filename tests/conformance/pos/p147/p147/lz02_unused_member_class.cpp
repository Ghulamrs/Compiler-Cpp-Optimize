// [temp.inst]/1: a member class's definition is instantiated only when used, so one ill-formed for this T is no error.
#include <cstdio>
template <class T> struct Box {
    struct Bad { typename T::nothing n; };
    struct Good { T g; };
    T v;
};
int main() { Box<int> b; b.v = 4; Box<int>::Good g; g.g = 5; std::printf("%d %d\n", b.v, g.g); return 0; }
