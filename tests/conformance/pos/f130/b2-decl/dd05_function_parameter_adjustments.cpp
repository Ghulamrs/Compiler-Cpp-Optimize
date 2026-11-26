#include <cstdio>
#include <type_traits>
void f(int a[10], int b[], int c(int), const int d, int *const e); template <class T> int kind(T);
int main() { std::printf("%d %d %d %d\n", (int)std::is_same<decltype(f), void(int*, int*, int(*)(int), int, int*const)>::value, (int)std::is_same<void(int), void(const int)>::value, (int)std::is_same<void(int[]), void(int*)>::value, (int)std::is_same<int(int(int)), int(int(*)(int))>::value); return 0; }
