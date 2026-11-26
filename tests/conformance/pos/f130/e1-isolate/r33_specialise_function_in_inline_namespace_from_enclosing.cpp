#include <cstdio>
namespace lib { inline namespace v2 { template <class T> int ver(T) { return 2; } } } namespace lib { template <> int ver(int) { return 22; } }
int main() { std::printf("%d %d\n", lib::ver(1), lib::ver(1.0)); return 0; }
