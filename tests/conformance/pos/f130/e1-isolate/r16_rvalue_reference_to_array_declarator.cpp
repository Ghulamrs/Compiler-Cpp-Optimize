#include <cstdio>
int main() { int (&&ra)[3] = {1, 2, 3}; std::printf("%d\n", ra[2]); return 0; }
