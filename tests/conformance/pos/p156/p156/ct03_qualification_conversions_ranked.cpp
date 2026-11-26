#include <atomic>
#include <cstdio>

// [over.ics.rank]/3.2.5-6: the target whose cv-qualification is a proper subset of the other's is better.
static int f(const int *) { return 1; }
static int f(const volatile int *) { return 2; }
static int g(const int &) { return 1; }
static int g(const volatile int &) { return 2; }
static int h(volatile int *) { return 1; }
static int h(const volatile int *) { return 2; }

int main() {
    int x = 0;
    volatile int vx = 0;
    std::atomic<int> a(5);
    std::printf("%d %d %d %d %d\n", f(&x), f(&vx), g(x), h(&x), std::atomic_load(&a));
    std::atomic_store(&a, 7);
    const int before = std::atomic_fetch_add(&a, 1);
    std::printf("%d %d\n", before, a.load());
    return 0;
}
