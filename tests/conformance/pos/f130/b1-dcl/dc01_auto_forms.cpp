#include <cstdio>
#include <type_traits>
#include <initializer_list>
const int ci = 1; int arr[2];
int main() { auto a = ci; auto &b = ci; const auto c = 2; auto *d = &a; auto e = {1, 2}; auto f = arr; auto &&g = 3; auto &&h = a;
std::printf("%d %d %d %d %d %d %d %d\n", (int)std::is_same<decltype(a), int>::value, (int)std::is_same<decltype(b), const int&>::value, (int)std::is_same<decltype(c), const int>::value, (int)std::is_same<decltype(d), int*>::value, (int)std::is_same<decltype(e), std::initializer_list<int>>::value, (int)std::is_same<decltype(f), int*>::value, (int)std::is_same<decltype(g), int&&>::value, (int)std::is_same<decltype(h), int&>::value); return 0; }
