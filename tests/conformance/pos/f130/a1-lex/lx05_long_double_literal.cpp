#include <cstdio>
int main() { long double x = 1.5L; long double y = x * 3; std::printf("%d %Lf %d\n", (int)sizeof(long double), y, (int)sizeof(1.0L)); return 0; }
