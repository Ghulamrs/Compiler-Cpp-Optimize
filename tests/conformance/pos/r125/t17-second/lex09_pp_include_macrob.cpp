// Computed includes, include guards, __has_include not in C++11 (skipped); include of a header twice; #pragma once in a header; a macro that expands to a header name.
#include <cstdio>
#define HDR <cstring>
#include HDR
#define PRAGMA_TEST
#pragma once
#pragma warning(disable : 4000)
#pragma unknown_pragma_is_ignored
int main() {
    std::printf("%d\n", (int)std::strlen("four"));
    std::printf("pragma op ok\n");
    return 0;
}
