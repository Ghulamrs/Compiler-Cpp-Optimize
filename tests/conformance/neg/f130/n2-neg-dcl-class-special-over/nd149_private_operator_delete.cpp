#include <cstddef>
class S { static void operator delete(void *); public: S() {} }; int main() { S *p = new S; delete p; return 0; }
