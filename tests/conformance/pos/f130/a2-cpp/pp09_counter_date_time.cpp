#include <cstdio>
int main() { std::printf("%d %d %d\n", (int)sizeof(__DATE__), (int)sizeof(__TIME__), __LINE__); return 0; }
