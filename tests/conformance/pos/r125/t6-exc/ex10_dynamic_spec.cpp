// Dynamic exception specifications (deprecated in C++11 but valid): throw(int) allows int; violation calls std::unexpected -> terminate; here only the allowed path and the unexpected handler.
#include <cstdio>
#include <cstdlib>
#include <exception>
void allowed() throw(int) { throw 5; }
int main() {
    try { allowed(); } catch (int v) { std::printf("%d\n", v); }
    return 0;
}
