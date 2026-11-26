// [basic.stc.thread]: each thread has its own object, initialised with the constant - the main thread's untouched by another's writes
#include <cstdio>
extern "C" void *CreateThread(void *, unsigned long long, unsigned long (*)(void *), void *, unsigned long, unsigned long *);
extern "C" unsigned long WaitForSingleObject(void *, unsigned long);
extern "C" int CloseHandle(void *);
static void inThread(unsigned long (*fn)(void *), void *arg) {
    void *h = CreateThread(0, 0, fn, arg, 0, 0);
    WaitForSingleObject(h, 0xFFFFFFFFu);
    CloseHandle(h);
}
thread_local int counter = 3;
thread_local long long big[4] = { 1, 2, 3, 4 };
unsigned long worker(void *out) {
    counter += 10; big[2] += 100;
    *static_cast<int *>(out) = counter * 1000 + static_cast<int>(big[2]);
    return 0;
}
int main() {
    counter++; big[2] = 7;
    int seen = 0;
    inThread(worker, &seen);
    std::printf("%d %d %lld\n", seen, counter, big[2]);
    return 0;
}
