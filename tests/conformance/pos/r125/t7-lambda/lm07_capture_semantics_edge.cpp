// Edge semantics: capturing a const variable not odr-used needs no capture; capturing by copy of a reference captures the referent; captured this through nested lambdas; capture of a static (no capture); array captured by reference.
#include <cstdio>
static int gs = 3;
struct W { int v = 7; int go() { auto o = [this] { auto i = [this] { return v; }; return i() + 1; }; return o(); } };
int main() {
    const int k = 4;
    auto noCapture = [] { return k + 1; };
    int x = 1; int &rx = x; auto refCopy = [rx] { return rx; }; x = 50;
    auto st = [] { return gs; }; gs = 30;
    int arr[2] = {1, 2}; auto ar = [&arr] { arr[0] = 9; return arr[0] + arr[1]; };
    int arv = ar(); std::printf("%d %d %d %d %d\n", noCapture(), refCopy(), st(), arv, arr[0]);
    W w; std::printf("%d\n", w.go());
    return 0;
}
