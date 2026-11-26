#include <cstdio>
namespace L { inline namespace V { struct T {}; int f(T) { return 5; } } }
int main() { L::T t; using L::f; std::printf("%d %d\n", f(t), L::V::f(t)); return 0; }
