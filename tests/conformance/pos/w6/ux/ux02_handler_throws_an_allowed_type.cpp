// [except.unexpected]/2: a handler that throws what the list allows - the search goes on at the call of the function
#include <cstdio>
#include <exception>
struct E { int v; };
void g() { throw 2.5; }
void f() throw(int, E) { g(); }
void onUnexpected() { std::printf("unexpected "); E e = { 7 }; throw e; }
int main() {
    std::set_unexpected(onUnexpected);
    try { f(); } catch (int) { std::printf("int\n"); } catch (E &e) { std::printf("E %d\n", e.v); }
    catch (double) { std::printf("double\n"); }
    return 0;
}
