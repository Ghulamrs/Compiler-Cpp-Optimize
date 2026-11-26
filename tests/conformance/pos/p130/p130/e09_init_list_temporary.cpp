#include <initializer_list>
int main() { return std::initializer_list<int>{4, 5}.size() == 2 ? 0 : 1; }
