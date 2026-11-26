#include <cstdio>
namespace N { struct P { int a, b; }; int counter = 10; enum Colour { red = 3, green }; }
int counter = 100;
int main() {
    using N::P;
    using N::counter;
    using N::green;
    P p = {1, 2};
    counter++;
    int s = p.a + p.b + counter + green;
    {
        int counter = 1;
        s += counter;
    }
    s += counter;
    std::printf("%d %d %d\n", s, N::counter, ::counter);
    return 0;
}
