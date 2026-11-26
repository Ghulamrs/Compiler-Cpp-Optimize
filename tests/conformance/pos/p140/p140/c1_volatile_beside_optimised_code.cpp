// A file that writes `volatile` at -O2: the functions that touch a volatile lvalue keep every access,
// in order and none merged, and the rest of the file is optimised as any other.
#include <cstdio>
volatile int ticks = 0;
static int reads = 0;
struct Port { volatile unsigned status; unsigned data; };
int poll(volatile int *p, int n) { int seen = 0; for (int i = 0; i < n; i++) { *p = *p + 1; seen += *p; } return seen; }
int square(int x) { return x * x; }
int sumSquares(int n) { int s = 0; for (int i = 1; i <= n; i++) s += square(i); return s; }
unsigned drain(Port &port) { unsigned got = 0; while (port.status != 0) { got += port.data; port.status = port.status - 1; } return got; }
int main() {
    int local = 0;
    for (int i = 0; i < 1000; i++) ticks = ticks + 1;
    int seen = poll(&ticks, 3);
    Port port = { 4, 7 };
    unsigned got = drain(port);
    volatile int v = 5;
    reads = v + v;
    local = sumSquares(10);
    std::printf("%d %d %u %u %d %d\n", ticks, seen, got, port.status, reads, local);
    return 0;
}