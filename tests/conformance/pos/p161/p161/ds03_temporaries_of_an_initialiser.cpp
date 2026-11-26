// [except.ctor]/1: a declaration's initialiser ends a full-expression as a statement does, and a temporary's destructor
// that throws there leaves the earlier temporaries, and the complete object the initialiser built, to be destroyed.
#include <cstdio>
struct Q { int id; explicit Q(int i) : id(i) {} ~Q() { std::printf("~Q%d ", id); } };
struct T { int id; explicit T(int i) : id(i) {} ~T() noexcept(false) { std::printf("~T%d ", id); throw id; } };
static int one(const T &) { return 1; }
struct Made { int v; explicit Made(const T &) : v(1) {} ~Made() { std::printf("~Made "); } };
int main() {
    try { int v = (Q(10), one(T(11))); std::printf("no %d ", v); } catch (int e) { std::printf("caught%d\n", e); }
    try { const int v = (Q(12), one(T(13))); std::printf("no %d ", v); } catch (int e) { std::printf("caught%d\n", e); }
    try { Made m(T(15)); std::printf("no %d ", m.v); } catch (int e) { std::printf("caught%d\n", e); }
    int r = 0;
    try { r = (Q(16), one(T(17))); } catch (int e) { std::printf("caught%d\n", e); }
    return r;
}
