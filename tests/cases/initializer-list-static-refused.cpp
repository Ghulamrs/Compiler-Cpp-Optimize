// A std::initializer_list with static storage made from braces would need its
// backing array to be static too - refused by name.
#include <initializer_list>
static std::initializer_list<int> g = {1, 2, 3};
int main() { return static_cast<int>(g.size()); }
