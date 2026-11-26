#include <cstdio>
void f() noexcept {} void (*pf)() noexcept = f; void (*pg)() throw() = f;
int main() { pf(); pg(); std::printf("ok\n"); return 0; }
