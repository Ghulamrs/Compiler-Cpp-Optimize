// [except.unexpected]/2: the list names std::bad_exception, so what the handler throws outside it becomes one
#include <cstdio>
#include <exception>
void f() throw(int, std::bad_exception) { throw 'c'; }
void onUnexpected() { std::printf("unexpected "); throw 2.5; }
int main() {
    std::set_unexpected(onUnexpected);
    try { f(); }
    catch (int) { std::printf("int\n"); }
    catch (std::bad_exception &) { std::printf("bad_exception\n"); }
    catch (double) { std::printf("double\n"); }
    return 0;
}
