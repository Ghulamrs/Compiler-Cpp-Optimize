#include <cstdio>
#include <type_traits>
int arr[3]; void fn(int) {}
int main() { auto a = arr; auto f = fn; auto &ra = arr; auto &rf = fn; std::printf("%d %d %d %d\n", (int)std::is_same<decltype(a), int*>::value, (int)std::is_same<decltype(f), void(*)(int)>::value, (int)std::is_same<decltype(ra), int(&)[3]>::value, (int)std::is_same<decltype(rf), void(&)(int)>::value); return 0; }
