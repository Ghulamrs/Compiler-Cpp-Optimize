// [temp.deduct]/8: enable_if<false, int> has no type, so small's only candidate drops out and nothing is left.
#include <type_traits>
template <class T, typename std::enable_if<sizeof(T) == 1, int>::type = 0> int small(T) { return 1; }
int main() { return small(5); }
