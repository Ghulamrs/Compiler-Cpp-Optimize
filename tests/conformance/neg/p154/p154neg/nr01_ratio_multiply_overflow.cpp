#include <cstdint>
#include <ratio>
typedef std::ratio_multiply<std::ratio<INTMAX_MAX, 1>, std::ratio<2, 1> > r;
int main() { return r::num == 0; }
