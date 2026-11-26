#include <cstdio>
#define F(...) std::printf(__VA_ARGS__)
#define G(a, ...) std::printf(a, __VA_ARGS__)
#define H(x, ...) #__VA_ARGS__
int main() { F("%d\n", 1); G("%d %d\n", 2, 3); std::printf("[%s][%s]\n", H(1,), H(1, 2, 3)); return 0; }
