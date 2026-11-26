#include <cstdio>
struct M { M() { throw 7; } }; struct S { M m; S() try : m() { std::printf("body "); } catch (int e) { std::printf("ftb%d ", e); } };
int f() try { throw 3; } catch (int e) { return e * 2; }
int main() { try { S s; } catch (int e) { std::printf("outer%d ", e); } std::printf("%d\n", f()); return 0; }
