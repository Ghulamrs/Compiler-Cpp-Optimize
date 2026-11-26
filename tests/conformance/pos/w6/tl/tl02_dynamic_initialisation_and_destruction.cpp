// [basic.start.init]/2, [basic.start.term]: a thread_local with a dynamic initialiser and a destructor, built and destroyed in each thread that uses it
#include <cstdio>
extern "C" void *CreateThread(void *, unsigned long long, unsigned long (*)(void *), void *, unsigned long, unsigned long *);
extern "C" unsigned long WaitForSingleObject(void *, unsigned long);
extern "C" int CloseHandle(void *);
static void inThread(unsigned long (*fn)(void *), void *arg) {
    void *h = CreateThread(0, 0, fn, arg, 0, 0);
    WaitForSingleObject(h, 0xFFFFFFFFu);
    CloseHandle(h);
}
int made = 0;
int next() { return ++made * 10; }
struct T { int v; T() : v(next()) { std::printf("T%d ", v); } ~T() { std::printf("~T%d ", v); } };
thread_local T t;
thread_local int n = next();
unsigned long worker(void *) { std::printf("w%d %d ", t.v, n); return 0; }
int main() {
    std::printf("m%d %d ", t.v, n);
    inThread(worker, 0);
    std::printf("| ");
    return 0;
}
