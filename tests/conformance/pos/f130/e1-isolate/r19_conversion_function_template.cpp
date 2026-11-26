#include <cstdio>
struct S { template <class T> operator T*() const { return nullptr; } }; struct Any { template <class T> operator T() const { return T(7); } };
int main() { S s; double *dp = s; Any a; int i = a; std::printf("%d %d\n", (int)(dp == nullptr), i); return 0; }
