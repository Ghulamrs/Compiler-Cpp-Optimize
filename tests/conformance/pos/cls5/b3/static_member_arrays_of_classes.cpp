#include <cstdio>
struct T { int v; T() : v(1) { std::printf("T%d ", v); } T(int x) : v(x) { std::printf("T%d ", v); } ~T() { std::printf("~T%d ", v); } };
struct S { static T arr[2]; static T init[3]; static const int n = 2; };
T S::arr[2];
T S::init[3] = {T(5), 6};
int main() {
    std::printf("| %d %d %d %d %d |", S::arr[0].v, S::arr[1].v, S::init[0].v, S::init[1].v, S::init[2].v);
    return 0;
}
