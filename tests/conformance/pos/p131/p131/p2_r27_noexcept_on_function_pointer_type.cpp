#include <cstdio>
void f() noexcept {} void (*pf)() noexcept = f; typedef void (*NX)(); void (*pg)() throw() = f;
int main() { pf(); pg(); NX n = f; n(); std::printf("ok\n"); return 0; }
