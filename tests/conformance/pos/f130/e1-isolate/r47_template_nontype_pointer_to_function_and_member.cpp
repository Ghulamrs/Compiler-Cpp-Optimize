#include <cstdio>
void fn() { std::printf("fn "); } struct S { int m = 3; }; template <void (*F)()> void call() { F(); } template <int S::*PM> int member(S &s) { return s.*PM; }
int main() { S s; call<fn>(); std::printf("%d\n", member<&S::m>(s)); return 0; }
