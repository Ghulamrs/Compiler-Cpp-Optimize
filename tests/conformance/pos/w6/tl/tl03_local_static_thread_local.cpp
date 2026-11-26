// [stmt.dcl]/4, [dcl.stc]/4: a block-scope thread_local, counted separately in each thread
#include <cstdio>
extern "C" void *CreateThread(void *, unsigned long long, unsigned long (*)(void *), void *, unsigned long, unsigned long *);
extern "C" unsigned long WaitForSingleObject(void *, unsigned long);
extern "C" int CloseHandle(void *);
static void inThread(unsigned long (*fn)(void *), void *arg) {
    void *h = CreateThread(0, 0, fn, arg, 0, 0);
    WaitForSingleObject(h, 0xFFFFFFFFu);
    CloseHandle(h);
}
int tick() { thread_local int calls = 0; return ++calls; }
unsigned long worker(void *out) { tick(); tick(); *static_cast<int *>(out) = tick(); return 0; }
int main() {
    tick();
    int other = 0;
    inThread(worker, &other);
    std::printf("%d %d\n", other, tick());
    return 0;
}
