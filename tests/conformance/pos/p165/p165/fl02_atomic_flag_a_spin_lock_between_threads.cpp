// A spin lock of atomic_flag: nothing is lost where four threads add under it, and no thread is let in beside another.
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
std::atomic_flag lock = ATOMIC_FLAG_INIT;
long total = 0;
int inside = 0;
int crowded = 0;
void work(int turns) {
    for (int i = 0; i < turns; i++) {
        while (lock.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
        inside++;
        if (inside != 1) crowded++;
        total += 1;
        inside--;
        lock.clear(std::memory_order_release);
    }
}
int main() {
    std::vector<std::thread> pool;
    for (int t = 0; t < 4; t++) pool.push_back(std::thread(work, 20000));
    for (std::size_t t = 0; t < pool.size(); t++) pool[t].join();
    std::printf("%ld %d\n", total, crowded);
    return 0;
}
