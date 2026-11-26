// [over.ics.rank]/3.3: user-defined conversions through the same conversion function are ranked by their second standard conversion.
#include <cstdio>
struct Lng { operator long() const { return 7; } };
const char *lng(long) { return "long"; }
const char *lng(int) { return "int"; }
const char *dbl(double) { return "double"; }
const char *dbl(long) { return "long"; }
int main() { Lng l; std::printf("%s %s\n", lng(l), dbl(l)); return 0; }
