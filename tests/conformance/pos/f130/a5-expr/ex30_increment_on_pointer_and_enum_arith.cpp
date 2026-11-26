#include <cstdio>
enum E { x = 1, y = 2, z = 4 };
int main() { E e = static_cast<E>(x | z); int v = e + 1; int arr[3] = {1, 2, 3}; int *p = arr; p++; ++p; std::printf("%d %d %d %d\n", e, v, *p, (int)(y * 3)); return 0; }
