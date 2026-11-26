// [class.temporary]/5: binding a reference to a subobject of a temporary extends the whole temporary - and the temporary must still be destroyed at the end of the scope.
#include <cstdio>
struct T { int v; T(int v) : v(v) { std::printf("+%d ", v); } ~T() { std::printf("-%d ", v); } };
struct Pair { T a; T b; Pair() : a(1), b(2) {} };
Pair make() { return Pair(); }
int main() {
    { const T &m = Pair().b; std::printf("[%d] ", m.v); } std::printf("| member-of-temp\n");
    { const int &v = make().a.v; std::printf("[%d] ", v); } std::printf("| scalar-member\n");
    { const T &p = Pair().a; (void)p; } std::printf("| unused\n");
    return 0;
}
