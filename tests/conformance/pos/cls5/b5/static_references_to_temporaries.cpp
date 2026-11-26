#include <cstdio>
int f() { return 11; }
const int &r = 5;
const int &q = f();
namespace N { const long &s = 7; }
int main() { static const double &d = 2.5; static const int &k = f() + 1; std::printf("%d %d %ld %g %d\n", r, q, N::s, d, k); return 0; }
