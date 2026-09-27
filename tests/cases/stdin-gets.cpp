// std::gets, which C++11 keeps in <cstdio> and C++14 removed: at the end of stdin it answers null.
#include <cstdio>

int main()
{
    char line[16];
    std::printf("gets %s\n", std::gets(line) ? "a line" : "null");
    return 0;
}
