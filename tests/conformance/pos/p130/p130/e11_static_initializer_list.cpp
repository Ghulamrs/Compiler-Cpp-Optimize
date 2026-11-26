#include <initializer_list>
static std::initializer_list<int> il = {1, 2, 3};
int main() { return il.size() == 3 ? 0 : 1; }
