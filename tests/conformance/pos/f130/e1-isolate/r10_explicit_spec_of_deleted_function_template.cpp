#include <cstdio>
template <class T> void gen(T) = delete; template <> void gen(char) { std::printf("char\n"); }
int main() { gen('c'); return 0; }
