// [except.ctor]/1, [class.dtor]/3: a noexcept(false) destructor that throws as its object goes out of scope; the object is not destroyed twice
#include <cstdio>
struct M { int n; M() : n(0) {} ~M() noexcept(false) { std::printf("~M "); throw 4; } };
int main() { try { M m; } catch (int e) { std::printf("caught%d\n", e); } return 0; }
