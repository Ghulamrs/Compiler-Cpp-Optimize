#include <cstddef>
struct S { static int operator new(std::size_t); }; int main() { return 0; }
