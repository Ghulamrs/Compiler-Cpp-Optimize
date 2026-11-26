// R21 with [except.ctor]/2: the elements built before the one that throws are destroyed, last first.
#include <cstdio>
#include <initializer_list>
struct T { int id; T(int i) : id(i) { if (i == 3) throw i; std::printf("+%d ", id); } ~T() { std::printf("-%d ", id); } };
int count(std::initializer_list<T> l) { return (int)l.size(); }
int main() {
    try { count({T(1), T(2), T(3)}); } catch (int e) { std::printf("caught%d ", e); }
    try { count({1, 2, 3, 4}); } catch (int e) { std::printf("caught%d", e); }
    std::printf("\n");
    return 0;
}