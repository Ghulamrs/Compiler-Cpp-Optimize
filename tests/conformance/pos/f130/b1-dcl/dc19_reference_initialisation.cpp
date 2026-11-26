#include <cstdio>
struct B { int v = 1; }; struct D : B { int w = 2; }; D mkd() { return D(); } struct Conv { operator int() const { return 8; } };
int main() { D d; B &b = d; const D &cb = mkd(); int &&r = 5; r++; const int &ci = 2.5; const double &cd = 3; Conv c; const int &cc = c; int arr2[2] = {7, 8}; int (&ar)[2] = arr2; const int (&il)[3] = {1, 2, 3}; int &&rr = int(4);
std::printf("%d %d %d %d %d %g %d %d %d %d\n", b.v, cb.w, r, ci, (int)cd, cd, cc, ar[1], il[2], rr); return 0; }
