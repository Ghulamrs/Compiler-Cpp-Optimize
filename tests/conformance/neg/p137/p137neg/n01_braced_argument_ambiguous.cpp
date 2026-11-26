// [over.ics.rank]: int to long and int to double are both conversions, so neither list is better.
#include <initializer_list>
int f(std::initializer_list<long>) { return 1; }
int f(std::initializer_list<double>) { return 2; }
int main() { return f({1}); }