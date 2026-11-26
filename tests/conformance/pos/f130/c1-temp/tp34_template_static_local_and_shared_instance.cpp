#include <cstdio>
template <class T> int &counter() { static int c = 0; return c; } template <class T> struct Reg { static int n; Reg() { n++; } }; template <class T> int Reg<T>::n = 0;
int main() { counter<int>()++; counter<int>()++; counter<char>()++; Reg<int> a, b; Reg<char> c; std::printf("%d %d %d %d\n", counter<int>(), counter<char>(), Reg<int>::n, Reg<char>::n); return 0; }
