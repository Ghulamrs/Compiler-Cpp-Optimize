// [atomics.flag]: test_and_set says what the flag held and leaves it set, clear leaves it clear, in each spelling.
#include <atomic>
#include <cstdio>
std::atomic_flag atFile = ATOMIC_FLAG_INIT;
struct Holder {
    char before;
    std::atomic_flag flag;
    char after;
};
int main() {
    bool first = atFile.test_and_set();
    bool second = atFile.test_and_set();
    std::printf("%d %d\n", first ? 1 : 0, second ? 1 : 0);
    atFile.clear();
    bool third = atFile.test_and_set(std::memory_order_acquire);
    std::printf("%d\n", third ? 1 : 0);
    atFile.clear(std::memory_order_release);

    volatile std::atomic_flag quick = ATOMIC_FLAG_INIT;
    bool v1 = quick.test_and_set();
    bool v2 = quick.test_and_set();
    std::printf("%d %d\n", v1 ? 1 : 0, v2 ? 1 : 0);
    quick.clear();

    std::atomic_flag freeFn = ATOMIC_FLAG_INIT;
    bool f1 = std::atomic_flag_test_and_set(&freeFn);
    bool f2 = std::atomic_flag_test_and_set_explicit(&freeFn, std::memory_order_seq_cst);
    std::printf("%d %d\n", f1 ? 1 : 0, f2 ? 1 : 0);
    std::atomic_flag_clear(&freeFn);
    bool f3 = std::atomic_flag_test_and_set(&freeFn);
    std::atomic_flag_clear_explicit(&freeFn, std::memory_order_release);
    bool f4 = std::atomic_flag_test_and_set(&freeFn);
    std::printf("%d %d\n", f3 ? 1 : 0, f4 ? 1 : 0);

    // Neighbouring bytes are not part of the exchange.
    Holder h;
    h.before = 'a';
    h.after = 'b';
    h.flag.clear();
    bool hf = h.flag.test_and_set();
    std::printf("%c %c %d\n", h.before, h.after, hf ? 1 : 0);

    std::printf("%d %d\n", noexcept(atFile.test_and_set()) ? 1 : 0, noexcept(atFile.clear()) ? 1 : 0);
    return 0;
}
