#include <cstddef>
class S { static void *operator new(std::size_t); public: S() {} }; int main() { S *p = new S; return 0; }
