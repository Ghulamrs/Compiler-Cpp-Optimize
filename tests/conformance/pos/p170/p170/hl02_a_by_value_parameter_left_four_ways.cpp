// [except.handle]/16: a handler's by-value parameter is destroyed when the handler is left - by its end, by a
// return, by an exception thrown from its block and by `throw;` - and the exception object after it. Until 1.69
// the parameter stood on x86_64-windows when an exception left the handler (found by p170's hl01).
#include <cstdio>
static int made = 0;
struct E {
    int id;
    E() : id(++made) { std::printf("E%d ", id); }
    E(const E &) : id(++made) { std::printf("E%d(copy) ", id); }
    ~E() { std::printf("~E%d ", id); }
};
void thrower(int x) { throw x; }
void leaveByThrow() { try { throw E(); } catch (E e) { std::printf("in(%d) ", e.id); thrower(7); } }
void leaveByEnd() { try { throw E(); } catch (E e) { std::printf("in(%d) ", e.id); } }
void leaveByReturn() { try { throw E(); } catch (E e) { std::printf("in(%d) ", e.id); return; } }
void rethrow() { try { throw E(); } catch (E e) { std::printf("in(%d) ", e.id); throw; } }
int main() {
    try { leaveByThrow(); } catch (int) { std::printf("| caught int\n"); }
    leaveByEnd(); std::printf("| end\n");
    leaveByReturn(); std::printf("| return\n");
    try { rethrow(); } catch (E &e) { std::printf("| caught E%d ", e.id); } std::printf("\n");
    return 0;
}
