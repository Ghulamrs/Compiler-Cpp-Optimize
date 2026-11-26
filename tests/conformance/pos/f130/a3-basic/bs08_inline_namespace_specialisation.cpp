#include <cstdio>
namespace lib { inline namespace v2 { template <class T> int ver(T) { return 2; } struct S {}; } namespace v1 { template <class T> int ver(T) { return 1; } } }
namespace lib { template <> int ver(int) { return 22; } }
int main() { std::printf("%d %d %d\n", lib::ver(1), lib::ver(1.0), lib::v1::ver(1)); lib::S s; (void)s; return 0; }
