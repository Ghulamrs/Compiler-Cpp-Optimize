// [except.ctor]: the locals of a handler are destroyed when an exception passes out through the handler
#include <cstdio>
struct T { int i; T(int i) : i(i) {} ~T() { std::printf("~%d ", i); } };
void thrower(int x) { throw x; }
int f() {
    try { thrower(1); }
    catch (int e) { T t(e); { T u(e + 1); thrower(e + 10); } }
    return 0;
}
int main() { try { f(); } catch (int e) { std::printf("caught%d\n", e); } return 0; }
