// 20000 throws through two frames each: correctness of repeated unwinding and the registration of handlers in loops; also a destructor that runs on each iteration.
#include <cstdio>
struct Count { static int n; ~Count() { ++n; } };
int Count::n = 0;
int work(int i) { Count c; if (i % 3 == 0) throw i; return i; }
int mid(int i) { Count c; return work(i) + 1; }
int main() {
    long long sum = 0; int caught = 0;
    for (int i = 0; i < 20000; ++i) { try { sum += mid(i); } catch (int v) { caught++; sum -= v; } }
    std::printf("%lld %d %d\n", sum, caught, Count::n);
    return 0;
}
